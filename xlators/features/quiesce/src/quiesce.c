/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "quiesce.h"
#include "defaults.h"
#include "call-stub.h"

/* TODO: */
/* Think about 'writev/_*_lk/setattr/xattrop/' fops to do re-transmittion */


/* Quiesce Specific Functions */
void
gf_quiesce_local_wipe (xlator_t *this, quiesce_local_t *local)
{
        if (!local || !this || !this->private)
                return;

        if (local->loc.inode)
                loc_wipe (&local->loc);
        if (local->fd)
                fd_unref (local->fd);
        GF_FREE (local->name);
        GF_FREE (local->volname);
        if (local->dict)
                dict_unref (local->dict);
        if (local->iobref)
                iobref_unref (local->iobref);
        GF_FREE (local->vector);

        mem_put (local);
}

call_stub_t *
gf_quiesce_dequeue (xlator_t *this)
{
        call_stub_t  *stub = NULL;
        quiesce_priv_t *priv = NULL;

        priv = this->private;

        if (!priv || list_empty (&priv->req))
                return NULL;

        LOCK (&priv->lock);
        {
                stub = list_entry (priv->req.next, call_stub_t, list);
                list_del_init (&stub->list);
                priv->queue_size--;
        }
        UNLOCK (&priv->lock);

        return stub;
}

void *
gf_quiesce_dequeue_start (void *data)
{
        xlator_t       *this = NULL;
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        this = data;
        priv = this->private;
        THIS = this;

        while (!list_empty (&priv->req)) {
                stub = gf_quiesce_dequeue (this);
                if (stub) {
                        call_resume (stub);
                }
        }

        return 0;
}


void
gf_quiesce_timeout (void *data)
{
        xlator_t       *this = NULL;
        quiesce_priv_t *priv = NULL;

        this = data;
        priv = this->private;
        THIS = this;

        LOCK (&priv->lock);
        {
                priv->pass_through = _gf_true;
        }
        UNLOCK (&priv->lock);

        gf_quiesce_dequeue_start (this);

        return;
}

void
gf_quiesce_enqueue (xlator_t *this, call_stub_t *stub)
{
        quiesce_priv_t *priv    = NULL;
        struct timespec timeout = {0,};

        priv = this->private;
        if (!priv) {
                gf_log_callingfn (this->name, GF_LOG_ERROR,
                                  "this->private == NULL");
                return;
        }

        LOCK (&priv->lock);
        {
                list_add_tail (&stub->list, &priv->req);
                priv->queue_size++;
        }
        UNLOCK (&priv->lock);

        if (!priv->timer) {
                timeout.tv_sec = 20;
                timeout.tv_nsec = 0;

                priv->timer = gf_timer_call_after (this->ctx,
                                                   timeout,
                                                   gf_quiesce_timeout,
                                                   (void *) this);
        }

        return;
}



/* _CBK function section */

int32_t
quiesce_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_lookup_stub (frame, default_lookup_resume,
                                        &local->loc, local->dict);
                if (!stub) {
                        STACK_UNWIND_STRICT (lookup, frame, -1, ENOMEM,
                                             NULL, NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             dict, postparent);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_stat_stub (frame, default_stat_resume,
                                      &local->loc, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (stat, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_access_stub (frame, default_access_resume,
                                        &local->loc, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (access, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *path,
                      struct iatt *buf, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_readlink_stub (frame, default_readlink_resume,
                                          &local->loc, local->size, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (readlink, frame, -1, ENOMEM,
                                             NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, buf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_open_stub (frame, default_open_resume,
                                      &local->loc, local->flag, local->fd,
                                      xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (open, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_readv_stub (frame, default_readv_resume,
                                       local->fd, local->size, local->offset,
                                       local->io_flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (readv, frame, -1, ENOMEM,
                                             NULL, 0, NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_flush_stub (frame, default_flush_resume,
                                       local->fd, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (flush, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}



int32_t
quiesce_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fsync_stub (frame, default_fsync_resume,
                                       local->fd, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fsync, frame, -1, ENOMEM,
                                             NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fstat_stub (frame, default_fstat_resume,
                                       local->fd, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fstat, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_opendir_stub (frame, default_opendir_resume,
                                         &local->loc, local->fd, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (opendir, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fsyncdir_stub (frame, default_fsyncdir_resume,
                                          local->fd, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fsyncdir, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_statfs_stub (frame, default_statfs_resume,
                                        &local->loc, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (statfs, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fgetxattr_stub (frame, default_fgetxattr_resume,
                                           local->fd, local->name, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fgetxattr, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


int32_t
quiesce_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_getxattr_stub (frame, default_getxattr_resume,
                                          &local->loc, local->name, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


int32_t
quiesce_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                       uint8_t *strong_checksum, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_rchecksum_stub (frame, default_rchecksum_resume,
                                           local->fd, local->offset, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (rchecksum, frame, -1, ENOMEM,
                                             0, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno, weak_checksum,
                             strong_checksum, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


int32_t
quiesce_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_readdir_stub (frame, default_readdir_resume,
                                         local->fd, local->size, local->offset, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (readdir, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


int32_t
quiesce_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *entries, dict_t *xdata)
{
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_readdirp_stub (frame, default_readdirp_resume,
                                          local->fd, local->size, local->offset,
                                          local->dict);
                if (!stub) {
                        STACK_UNWIND_STRICT (readdirp, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


#if 0

int32_t
quiesce_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_writev_stub (frame, default_writev_resume,
                                        local->fd, local->vector, local->flag,
                                        local->offset, local->io_flags,
                                        local->iobref, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM,
                                             NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_xattrop_stub (frame, default_xattrop_resume,
                                         &local->loc, local->xattrop_flags,
                                         local->dict, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (xattrop, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fxattrop_stub (frame, default_fxattrop_resume,
                                          local->fd, local->xattrop_flags,
                                          local->dict, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fxattrop, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_lk_stub (frame, default_lk_resume,
                                    local->fd, local->flag, &local->flock, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (lk, frame, -1, ENOMEM,
                                             NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_inodelk_stub (frame, default_inodelk_resume,
                                         local->volname, &local->loc,
                                         local->flag, &local->flock, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (inodelk, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}


int32_t
quiesce_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_finodelk_stub (frame, default_finodelk_resume,
                                         local->volname, local->fd,
                                         local->flag, &local->flock, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (finodelk, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_entrylk_stub (frame, default_entrylk_resume,
                                         local->volname, &local->loc,
                                         local->name, local->cmd, local->type, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (entrylk, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fentrylk_stub (frame, default_fentrylk_resume,
                                          local->volname, local->fd,
                                          local->name, local->cmd, local->type, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOMEM, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;
        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_setattr_stub (frame, default_setattr_resume,
                                         &local->loc, &local->stbuf, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM,
                                             NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre,
                             statpost, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

int32_t
quiesce_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                      struct iatt *statpost, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        local = frame->local;
        frame->local = NULL;

        if ((op_ret == -1) && (op_errno == ENOTCONN)) {
                /* Re-transmit (by putting in the queue) */
                stub = fop_fsetattr_stub (frame, default_fsetattr_resume,
                                          local->fd, &local->stbuf, local->flag, xdata);
                if (!stub) {
                        STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM,
                                             NULL, NULL, NULL);
                        goto out;
                }

                gf_quiesce_enqueue (this, stub);
                goto out;
        }

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, statpre,
                             statpost, xdata);
out:
        gf_quiesce_local_wipe (this, local);

        return 0;
}

#endif /* if 0 */


/* FOP */

/* No retransmittion */

int32_t
quiesce_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_removexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            loc,
                            name, xdata);
                return 0;
        }

        stub = fop_removexattr_stub (frame, default_removexattr_resume,
                                     loc, name, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (removexattr, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_truncate (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  off_t offset, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            loc,
                            offset, xdata);
                return 0;
        }

        stub = fop_truncate_stub (frame, default_truncate_resume, loc, offset, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fsetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_fsetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            fd,
                            dict,
                            flags, xdata);
                return 0;
        }

        stub = fop_fsetxattr_stub (frame, default_fsetxattr_resume,
                                   fd, dict, flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_setxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  dict_t *dict,
		  int32_t flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            loc,
                            dict,
                            flags, xdata);
                return 0;
        }

        stub = fop_setxattr_stub (frame, default_setxattr_resume,
                                  loc, dict, flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (setxattr, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_create (call_frame_t *frame, xlator_t *this,
		loc_t *loc, int32_t flags, mode_t mode,
                mode_t umask, fd_t *fd, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                /* Don't send O_APPEND below, as write() re-transmittions can
                   fail with O_APPEND */
                STACK_WIND (frame, default_create_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->create,
                            loc, (flags & ~O_APPEND), mode, umask, fd, xdata);
                return 0;
        }

        stub = fop_create_stub (frame, default_create_resume,
                                loc, (flags & ~O_APPEND), mode, umask, fd, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (create, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_link (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_link_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->link,
                            oldloc, newloc, xdata);
                return 0;
        }

        stub = fop_link_stub (frame, default_link_resume, oldloc, newloc, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (link, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_rename (call_frame_t *frame,
		xlator_t *this,
		loc_t *oldloc,
		loc_t *newloc, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_rename_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename,
                            oldloc, newloc, xdata);
                return 0;
        }

        stub = fop_rename_stub (frame, default_rename_resume, oldloc, newloc, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (rename, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int
quiesce_symlink (call_frame_t *frame, xlator_t *this,
		 const char *linkpath, loc_t *loc, mode_t umask, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_symlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->symlink,
                            linkpath, loc, umask, xdata);
                return 0;
        }

        stub = fop_symlink_stub (frame, default_symlink_resume,
                                 linkpath, loc, umask, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (symlink, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int
quiesce_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_rmdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rmdir,
                            loc, flags, xdata);
                return 0;
        }

        stub = fop_rmdir_stub (frame, default_rmdir_resume, loc, flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_unlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc, int xflag, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_unlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            loc, xflag, xdata);
                return 0;
        }

        stub = fop_unlink_stub (frame, default_unlink_resume, loc, xflag, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int
quiesce_mkdir (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir,
                            loc, mode, umask, xdata);
                return 0;
        }

        stub = fop_mkdir_stub (frame, default_mkdir_resume,
                               loc, mode, umask, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int
quiesce_mknod (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame, default_mknod_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mknod,
                            loc, mode, rdev, umask, xdata);
                return 0;
        }

        stub = fop_mknod_stub (frame, default_mknod_resume,
                               loc, mode, rdev, umask, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (mknod, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_ftruncate (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   off_t offset, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv->pass_through) {
                STACK_WIND (frame,
                            default_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd,
                            offset, xdata);
                return 0;
        }

        stub = fop_ftruncate_stub (frame, default_ftruncate_resume, fd, offset, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

/* Re-transmittion */

int32_t
quiesce_readlink (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  size_t size, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                local->size = size;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_readlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readlink,
                            loc,
                            size, xdata);
                return 0;
        }

        stub = fop_readlink_stub (frame, default_readlink_resume, loc, size, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (readlink, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_access (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t mask, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                local->flag = mask;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_access_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->access,
                            loc,
                            mask, xdata);
                return 0;
        }

        stub = fop_access_stub (frame, default_access_resume, loc, mask, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (access, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fgetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   const char *name, dict_t *xdata)
{
        quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                if (name)
                        local->name = gf_strdup (name);

                frame->local = local;

                STACK_WIND (frame,
                            quiesce_fgetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fgetxattr,
                            fd,
                            name, xdata);
                return 0;
        }

        stub = fop_fgetxattr_stub (frame, default_fgetxattr_resume, fd, name, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_statfs (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_statfs_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->statfs,
                            loc, xdata);
                return 0;
        }

        stub = fop_statfs_stub (frame, default_statfs_resume, loc, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (statfs, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fsyncdir (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  int32_t flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->flag = flags;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_fsyncdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsyncdir,
                            fd,
                            flags, xdata);
                return 0;
        }

        stub = fop_fsyncdir_stub (frame, default_fsyncdir_resume, fd, flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_opendir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc, fd_t *fd, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                local->fd = fd_ref (fd);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_opendir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir,
                            loc, fd, xdata);
                return 0;
        }

        stub = fop_opendir_stub (frame, default_opendir_resume, loc, fd, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (opendir, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fstat (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_fstat_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fstat,
                            fd, xdata);
                return 0;
        }

        stub = fop_fstat_stub (frame, default_fstat_resume, fd, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fstat, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fsync (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       int32_t flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->flag = flags;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_fsync_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsync,
                            fd,
                            flags, xdata);
                return 0;
        }

        stub = fop_fsync_stub (frame, default_fsync_resume, fd, flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fsync, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_flush (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_flush_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd, xdata);
                return 0;
        }

        stub = fop_flush_stub (frame, default_flush_resume, fd, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (flush, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_writev (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		struct iovec *vector,
		int32_t count,
		off_t off, uint32_t flags,
                struct iobref *iobref, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_writev_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev,
                            fd,
                            vector,
                            count,
                            off, flags,
                            iobref, xdata);
                return 0;
        }

        stub = fop_writev_stub (frame, default_writev_resume,
                                fd, vector, count, off, flags, iobref, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_readv (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset, uint32_t flags, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->size = size;
                local->offset = offset;
                local->io_flag = flags;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_readv_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv,
                            fd,
                            size,
                            offset, flags, xdata);
                return 0;
        }

        stub = fop_readv_stub (frame, default_readv_resume, fd, size, offset,
                               flags, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (readv, frame, -1, ENOMEM,
                                     NULL, 0, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_open (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags, fd_t *fd,
              dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                local->fd = fd_ref (fd);

                /* Don't send O_APPEND below, as write() re-transmittions can
                   fail with O_APPEND */
                local->flag = (flags & ~O_APPEND);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            loc, (flags & ~O_APPEND), fd, xdata);
                return 0;
        }

        stub = fop_open_stub (frame, default_open_resume, loc,
                              (flags & ~O_APPEND), fd, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (open, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                if (name)
                        local->name = gf_strdup (name);

                frame->local = local;

                STACK_WIND (frame,
                            quiesce_getxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr,
                            loc,
                            name, xdata);
                return 0;
        }

        stub = fop_getxattr_stub (frame, default_getxattr_resume, loc, name, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_xattrop (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 gf_xattrop_flags_t flags,
		 dict_t *dict, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_xattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop,
                            loc,
                            flags,
                            dict, xdata);
                return 0;
        }

        stub = fop_xattrop_stub (frame, default_xattrop_resume,
                                 loc, flags, dict, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (xattrop, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fxattrop (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  gf_xattrop_flags_t flags,
		  dict_t *dict, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_fxattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fxattrop,
                            fd,
                            flags,
                            dict, xdata);
                return 0;
        }

        stub = fop_fxattrop_stub (frame, default_fxattrop_resume,
                                  fd, flags, dict, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fxattrop, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_lk (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    int32_t cmd,
	    struct gf_flock *lock, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_lk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lk,
                            fd,
                            cmd,
                            lock, xdata);
                return 0;
        }

        stub = fop_lk_stub (frame, default_lk_resume, fd, cmd, lock, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (lk, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_inodelk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, int32_t cmd,
                 struct gf_flock *lock, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_inodelk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->inodelk,
                            volume, loc, cmd, lock, xdata);
                return 0;
        }

        stub = fop_inodelk_stub (frame, default_inodelk_resume,
                                 volume, loc, cmd, lock, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (inodelk, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_finodelk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_finodelk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->finodelk,
                            volume, fd, cmd, lock, xdata);
                return 0;
        }

        stub = fop_finodelk_stub (frame, default_finodelk_resume,
                                  volume, fd, cmd, lock, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (finodelk, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_entrylk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, const char *basename,
		 entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame, default_entrylk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->entrylk,
                            volume, loc, basename, cmd, type, xdata);
                return 0;
        }

        stub = fop_entrylk_stub (frame, default_entrylk_resume,
                                 volume, loc, basename, cmd, type, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (entrylk, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fentrylk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, const char *basename,
		  entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame, default_fentrylk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fentrylk,
                            volume, fd, basename, cmd, type, xdata);
                return 0;
        }

        stub = fop_fentrylk_stub (frame, default_fentrylk_resume,
                                  volume, fd, basename, cmd, type, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOMEM, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_rchecksum (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd, off_t offset,
                   int32_t len, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->offset = offset;
                local->flag = len;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_rchecksum_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rchecksum,
                            fd, offset, len, xdata);
                return 0;
        }

        stub = fop_rchecksum_stub (frame, default_rchecksum_resume,
                                   fd, offset, len, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (rchecksum, frame, -1, ENOMEM, 0, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_readdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t off, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->size = size;
                local->offset = off;
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_readdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir,
                            fd, size, off, xdata);
                return 0;
        }

        stub = fop_readdir_stub (frame, default_readdir_resume, fd, size, off, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (readdir, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_readdirp (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  size_t size,
		  off_t off, dict_t *dict)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                local->fd = fd_ref (fd);
                local->size = size;
                local->offset = off;
                local->dict = dict_ref (dict);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, off, dict);
                return 0;
        }

        stub = fop_readdirp_stub (frame, default_readdirp_resume, fd, size,
                                  off, dict);
        if (!stub) {
                STACK_UNWIND_STRICT (readdirp, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_setattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 struct iatt *stbuf,
                 int32_t valid, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_setattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr,
                            loc, stbuf, valid, xdata);
                return 0;
        }

        stub = fop_setattr_stub (frame, default_setattr_resume,
                                   loc, stbuf, valid, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}


int32_t
quiesce_stat (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_stat_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->stat,
                            loc, xdata);
                return 0;
        }

        stub = fop_stat_stub (frame, default_stat_resume, loc, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (stat, frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_lookup (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *xattr_req)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;
        quiesce_local_t *local = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                local = mem_get0 (priv->local_pool);
                loc_dup (loc, &local->loc);
                local->dict = dict_ref (xattr_req);
                frame->local = local;

                STACK_WIND (frame,
                            quiesce_lookup_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            loc, xattr_req);
                return 0;
        }

        stub = fop_lookup_stub (frame, default_lookup_resume, loc, xattr_req);
        if (!stub) {
                STACK_UNWIND_STRICT (lookup, frame, -1, ENOMEM,
                                     NULL, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
quiesce_fsetattr (call_frame_t *frame,
                  xlator_t *this,
                  fd_t *fd,
                  struct iatt *stbuf,
                  int32_t valid, dict_t *xdata)
{
	quiesce_priv_t *priv = NULL;
        call_stub_t    *stub = NULL;

        priv = this->private;

        if (priv && priv->pass_through) {
                STACK_WIND (frame,
                            default_fsetattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr,
                            fd, stbuf, valid, xdata);
                return 0;
        }

        stub = fop_fsetattr_stub (frame, default_fsetattr_resume,
                                  fd, stbuf, valid, xdata);
        if (!stub) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);
                return 0;
        }

        gf_quiesce_enqueue (this, stub);

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_quiesce_mt_end + 1);

        return ret;
}

int
init (xlator_t *this)
{
        int ret = -1;
        quiesce_priv_t *priv = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"'quiesce' not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_quiesce_mt_priv_t);
        if (!priv)
                goto out;

        priv->local_pool =  mem_pool_new (quiesce_local_t,
                                          GF_FOPS_EXPECTED_IN_PARALLEL);

        LOCK_INIT (&priv->lock);
        priv->pass_through = _gf_false;

        INIT_LIST_HEAD (&priv->req);

        this->private = priv;
        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        quiesce_priv_t *priv = NULL;

        priv = this->private;
        if (!priv)
                goto out;
        this->private = NULL;

        mem_pool_destroy (priv->local_pool);
        LOCK_DESTROY (&priv->lock);
        GF_FREE (priv);
out:
        return;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        int             ret     = 0;
        quiesce_priv_t *priv    = NULL;
        struct timespec timeout = {0,};

        priv = this->private;
        if (!priv)
                goto out;

        switch (event) {
        case GF_EVENT_CHILD_UP:
        {
                ret = pthread_create (&priv->thr, NULL, gf_quiesce_dequeue_start,
                                      this);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to create the quiesce-dequeue thread");
                }

                LOCK (&priv->lock);
                {
                        priv->pass_through = _gf_true;
                }
                UNLOCK (&priv->lock);
                break;
        }
        case GF_EVENT_CHILD_DOWN:
                LOCK (&priv->lock);
                {
                        priv->pass_through = _gf_false;
                }
                UNLOCK (&priv->lock);

                if (priv->timer)
                        break;
                timeout.tv_sec = 20;
                timeout.tv_nsec = 0;

                priv->timer = gf_timer_call_after (this->ctx,
                                                   timeout,
                                                   gf_quiesce_timeout,
                                                   (void *) this);

                if (priv->timer == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Cannot create timer");
                }

                break;
        default:
                break;
        }

        ret = default_notify (this, event, data);
out:
        return ret;
}


struct xlator_fops fops = {
        /* write/modifying fops */
	.mknod       = quiesce_mknod,
	.create      = quiesce_create,
	.truncate    = quiesce_truncate,
	.ftruncate   = quiesce_ftruncate,
	.setxattr    = quiesce_setxattr,
	.removexattr = quiesce_removexattr,
	.symlink     = quiesce_symlink,
	.unlink      = quiesce_unlink,
	.link        = quiesce_link,
	.mkdir       = quiesce_mkdir,
	.rmdir       = quiesce_rmdir,
	.rename      = quiesce_rename,

        /* The below calls are known to change state, hence
           re-transmittion is not advised */
	.lk          = quiesce_lk,
	.inodelk     = quiesce_inodelk,
	.finodelk    = quiesce_finodelk,
	.entrylk     = quiesce_entrylk,
	.fentrylk    = quiesce_fentrylk,
	.xattrop     = quiesce_xattrop,
	.fxattrop    = quiesce_fxattrop,
        .setattr     = quiesce_setattr,
        .fsetattr    = quiesce_fsetattr,

        /* Special case, re-transmittion is not harmful *
         * as offset is properly sent from above layers */
        /* TODO: not re-transmitted as of now */
	.writev      = quiesce_writev,

        /* re-transmittable fops */
	.lookup      = quiesce_lookup,
	.stat        = quiesce_stat,
	.fstat       = quiesce_fstat,
	.access      = quiesce_access,
	.readlink    = quiesce_readlink,
	.getxattr    = quiesce_getxattr,
	.open        = quiesce_open,
	.readv       = quiesce_readv,
	.flush       = quiesce_flush,
	.fsync       = quiesce_fsync,
	.statfs      = quiesce_statfs,
	.opendir     = quiesce_opendir,
	.readdir     = quiesce_readdir,
	.readdirp    = quiesce_readdirp,
	.fsyncdir    = quiesce_fsyncdir,

};

struct xlator_dumpops dumpops;


struct xlator_cbks cbks;


struct volume_options options[] = {
	{ .key  = {NULL} },
};
