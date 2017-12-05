/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

#include "ec.h"
#include "ec-messages.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"
#include "ec-method.h"
#include "ec-fops.h"
#include "ec-mem-types.h"

int32_t
ec_update_writev_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int32_t op_ret, int32_t op_errno,
                      struct iatt *prebuf, struct iatt *postbuf,
                      dict_t *xdata)
{
    ec_fop_data_t *fop    = cookie;
    ec_cbk_data_t *cbk    = NULL;
    ec_fop_data_t *parent = fop->parent;
    int           i       = 0;

    ec_trace("UPDATE_WRITEV_CBK", cookie, "ret=%d, errno=%d, parent-fop=%s",
             op_ret, op_errno, ec_fop_name (parent->id));

    if (op_ret < 0) {
            ec_fop_set_error (parent, op_errno);
            goto out;
    }
    cbk = ec_cbk_data_allocate (parent->frame, this, parent,
                                parent->id, 0, op_ret, op_errno);
    if (!cbk) {
            ec_fop_set_error (parent, ENOMEM);
            goto out;
    }

    if (xdata)
            cbk->xdata = dict_ref (xdata);

    if (prebuf)
            cbk->iatt[i++] = *prebuf;

    if (postbuf)
            cbk->iatt[i++] = *postbuf;

    LOCK (&parent->lock);
    {
            parent->good &= fop->good;

            if (gf_bits_count (parent->good) < parent->minimum) {
                    __ec_fop_set_error (parent, EIO);
            } else if (fop->error == 0 && parent->answer == NULL) {
                    parent->answer = cbk;
            }
    }
    UNLOCK (&parent->lock);
out:
    return 0;
}

int32_t ec_update_write(ec_fop_data_t *fop, uintptr_t mask, off_t offset,
                        size_t size)
{
    struct iobref *iobref = NULL;
    struct iobuf *iobuf = NULL;
    struct iovec vector;
    int32_t err = -ENOMEM;

    iobref = iobref_new();
    if (iobref == NULL) {
        goto out;
    }
    iobuf = iobuf_get(fop->xl->ctx->iobuf_pool);
    if (iobuf == NULL) {
        goto out;
    }
    err = iobref_add(iobref, iobuf);
    if (err != 0) {
        goto out;
    }

    vector.iov_base = iobuf->ptr;
    vector.iov_len = size;
    memset(vector.iov_base, 0, vector.iov_len);

    ec_writev(fop->frame, fop->xl, mask, fop->minimum,
              ec_update_writev_cbk, NULL, fop->fd, &vector, 1,
              offset, 0, iobref, NULL);

    err = 0;

out:
    if (iobuf != NULL) {
        iobuf_unref(iobuf);
    }
    if (iobref != NULL) {
        iobref_unref(iobref);
    }

    return err;
}

int
ec_inode_write_cbk (call_frame_t *frame, xlator_t *this, void *cookie,
                    int op_ret, int op_errno, struct iatt *prestat,
                    struct iatt *poststat, dict_t *xdata)
{
        ec_fop_data_t *fop = NULL;
        ec_cbk_data_t *cbk = NULL;
        int           i    = 0;
        int           idx  = 0;

        VALIDATE_OR_GOTO (this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, frame->local, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        fop = frame->local;
        idx = (int32_t)(uintptr_t) cookie;

        ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
                 frame, op_ret, op_errno);

        cbk = ec_cbk_data_allocate (frame, this, fop, fop->id, idx, op_ret,
                                    op_errno);
        if (!cbk)
                goto out;

        if (op_ret < 0)
                goto out;

        if (xdata)
                cbk->xdata = dict_ref (xdata);

        if (prestat)
                cbk->iatt[i++] = *prestat;

        if (poststat)
                cbk->iatt[i++] = *poststat;

out:
        if (cbk)
                ec_combine (cbk, ec_combine_write);

        if (fop)
                ec_complete (fop);
        return 0;
}
/* FOP: removexattr */

int32_t ec_removexattr_cbk (call_frame_t *frame, void *cookie,
                            xlator_t *this, int32_t op_ret, int32_t op_errno,
                            dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   NULL, NULL, xdata);
}

void ec_wind_removexattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_removexattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->removexattr,
                      &fop->loc[0], fop->str[0], fop->xdata);
}

void
ec_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, dict_t *xdata)
{
        ec_fop_data_t *fop = cookie;
        switch (fop->id) {
        case GF_FOP_SETXATTR:
                if (fop->cbks.setxattr) {
                        fop->cbks.setxattr (frame, cookie, this, op_ret,
                                            op_errno, xdata);
                }
                break;
        case GF_FOP_REMOVEXATTR:
                if (fop->cbks.removexattr) {
                        fop->cbks.removexattr (frame, cookie, this, op_ret,
                                               op_errno, xdata);
                }
                break;
        case GF_FOP_FSETXATTR:
                if (fop->cbks.fsetxattr) {
                        fop->cbks.fsetxattr (frame, cookie, this, op_ret,
                                             op_errno, xdata);
                }
                break;
        case GF_FOP_FREMOVEXATTR:
                if (fop->cbks.fremovexattr) {
                        fop->cbks.fremovexattr (frame, cookie, this, op_ret,
                                                op_errno, xdata);
                }
                break;
        }
}

int32_t
ec_manager_xattr (ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t * cbk;

        switch (state) {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
                if (fop->fd == NULL) {
                        ec_lock_prepare_inode(fop, &fop->loc[0],
                                              EC_UPDATE_META | EC_QUERY_INFO,
                                              0, LLONG_MAX);
                } else {
                        ec_lock_prepare_fd(fop, fop->fd,
                                           EC_UPDATE_META | EC_QUERY_INFO,
                                           0, LLONG_MAX);
                }
                ec_lock(fop);

                return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
                ec_dispatch_all(fop);

                return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
                ec_fop_prepare_answer(fop, _gf_false);

                return EC_STATE_REPORT;

        case EC_STATE_REPORT:
                cbk = fop->answer;

                GF_ASSERT(cbk != NULL);

                ec_xattr_cbk (fop->req_frame, fop, fop->xl, cbk->op_ret,
                              cbk->op_errno, cbk->xdata);

                    return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
                GF_ASSERT(fop->error != 0);

                ec_xattr_cbk (fop->req_frame, fop, fop->xl, -1, fop->error,
                              NULL);

                return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK_REUSE:
        case EC_STATE_LOCK_REUSE:
                ec_lock_reuse(fop);

                return EC_STATE_UNLOCK;

        case -EC_STATE_UNLOCK:
        case EC_STATE_UNLOCK:
                ec_unlock(fop);

                return EC_STATE_END;

        default:
                gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                        EC_MSG_UNHANDLED_STATE,
                        "Unhandled state %d for %s",
                        state, ec_fop_name(fop->id));

                return EC_STATE_END;
        }
}

void
ec_removexattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
                int32_t minimum, fop_removexattr_cbk_t func, void *data,
                loc_t *loc, const char *name, dict_t *xdata)
{
    ec_cbk_t callback = { .removexattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(REMOVEXATTR) %p", frame);

    VALIDATE_OR_GOTO (this, out);
    GF_VALIDATE_OR_GOTO (this->name, frame, out);
    GF_VALIDATE_OR_GOTO (this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_REMOVEXATTR, 0, target,
                               minimum, ec_wind_removexattr, ec_manager_xattr,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL,
                    "Failed to copy a location.");

            goto out;
        }
    }
    if (name != NULL) {
        fop->str[0] = gf_strdup(name);
        if (fop->str[0] == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_NO_MEMORY,
                    "Failed to duplicate a string.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: fremovexattr */

int32_t ec_fremovexattr_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno,
                             dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   NULL, NULL, xdata);
}

void ec_wind_fremovexattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fremovexattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fremovexattr,
                      fop->fd, fop->str[0], fop->xdata);
}

void
ec_fremovexattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
                 int32_t minimum, fop_fremovexattr_cbk_t func, void *data,
                 fd_t *fd, const char *name, dict_t *xdata)
{
    ec_cbk_t callback = { .fremovexattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FREMOVEXATTR) %p", frame);

    VALIDATE_OR_GOTO (this, out);
    GF_VALIDATE_OR_GOTO (this->name, frame, out);
    GF_VALIDATE_OR_GOTO (this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FREMOVEXATTR, 0, target,
                               minimum, ec_wind_fremovexattr, ec_manager_xattr,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL,
                    "Failed to reference a "
                    "file descriptor.");

            goto out;
        }
    }
    if (name != NULL) {
        fop->str[0] = gf_strdup(name);
        if (fop->str[0] == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_NO_MEMORY,
                    "Failed to duplicate a string.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: setattr */

int32_t ec_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt *prestat, struct iatt *poststat,
                        dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prestat, poststat, xdata);
}

void ec_wind_setattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_setattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->setattr,
                      &fop->loc[0], &fop->iatt, fop->int32, fop->xdata);
}

int32_t ec_manager_setattr(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            if (fop->fd == NULL) {
                ec_lock_prepare_inode(fop, &fop->loc[0],
                                      EC_UPDATE_META | EC_QUERY_INFO,
                                      0, LLONG_MAX);
            } else {
                ec_lock_prepare_fd(fop, fop->fd, EC_UPDATE_META | EC_QUERY_INFO,
                                   0, LLONG_MAX);
            }
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                if (cbk->iatt[0].ia_type == IA_IFREG) {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                    cbk->count);

                    /* This shouldn't fail because we have the inode locked. */
                    GF_ASSERT(ec_get_inode_size(fop,
                                                fop->locks[0].lock->loc.inode,
                                                &cbk->iatt[0].ia_size));
                    cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == GF_FOP_SETATTR)
            {
                if (fop->cbks.setattr != NULL)
                {
                    fop->cbks.setattr(fop->req_frame, fop, fop->xl,
                                      cbk->op_ret, cbk->op_errno,
                                      &cbk->iatt[0], &cbk->iatt[1],
                                      cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fsetattr != NULL)
                {
                    fop->cbks.fsetattr(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno,
                                       &cbk->iatt[0], &cbk->iatt[1],
                                       cbk->xdata);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_SETATTR)
            {
                if (fop->cbks.setattr != NULL)
                {
                    fop->cbks.setattr(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL, NULL, NULL);
                }
            }
            else
            {
                if (fop->cbks.fsetattr != NULL)
                {
                    fop->cbks.fsetattr(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL, NULL, NULL);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK_REUSE:
        case EC_STATE_LOCK_REUSE:
            ec_lock_reuse(fop);

            return EC_STATE_UNLOCK;

        case -EC_STATE_UNLOCK:
        case EC_STATE_UNLOCK:
            ec_unlock(fop);

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE,
                    "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_setattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_setattr_cbk_t func, void * data,
                loc_t * loc, struct iatt * stbuf, int32_t valid,
                dict_t * xdata)
{
    ec_cbk_t callback = { .setattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(SETATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SETATTR, 0, target, minimum,
                               ec_wind_setattr, ec_manager_setattr, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = valid;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL,
                    "Failed to copy a location.");

            goto out;
        }
    }
    if (stbuf != NULL) {
        fop->iatt = *stbuf;
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/* FOP: fsetattr */

int32_t ec_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt *prestat, struct iatt *poststat,
                         dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prestat, poststat, xdata);
}

void ec_wind_fsetattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fsetattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fsetattr,
                      fop->fd, &fop->iatt, fop->int32, fop->xdata);
}

void ec_fsetattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fsetattr_cbk_t func, void * data,
                 fd_t * fd, struct iatt * stbuf, int32_t valid, dict_t * xdata)
{
    ec_cbk_t callback = { .fsetattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FSETATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSETATTR, 0, target,
                               minimum, ec_wind_fsetattr, ec_manager_setattr,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = valid;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL,
                    "Failed to reference a "
                    "file descriptor.");

            goto out;
        }
    }
    if (stbuf != NULL) {
        fop->iatt = *stbuf;
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/* FOP: setxattr */

int32_t ec_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   NULL, NULL, xdata);
}

void ec_wind_setxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_setxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->setxattr,
                      &fop->loc[0], fop->dict, fop->int32, fop->xdata);
}

void
ec_setxattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
             int32_t minimum, fop_setxattr_cbk_t func, void *data,
             loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata)
{
    ec_cbk_t callback = { .setxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(SETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SETXATTR, 0, target,
                               minimum, ec_wind_setxattr, ec_manager_xattr,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = flags;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL,
                    "Failed to copy a location.");

            goto out;
        }
    }
    if (dict != NULL) {
        fop->dict = dict_copy_with_ref(dict, NULL);
        if (fop->dict == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: fsetxattr */

int32_t
ec_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO (this, out);
    GF_VALIDATE_OR_GOTO (this->name, frame, out);
    GF_VALIDATE_OR_GOTO (this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO (this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSETXATTR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL,
                        "Failed to reference a "
                        "dictionary.");

                goto out;
            }
        }

        ec_combine(cbk, NULL);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fsetxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fsetxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fsetxattr,
                      fop->fd, fop->dict, fop->int32, fop->xdata);
}

void
ec_fsetxattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
              int32_t minimum, fop_fsetxattr_cbk_t func, void *data,
              fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata)
{
    ec_cbk_t callback = { .fsetxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FSETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSETXATTR, 0, target,
                               minimum, ec_wind_fsetxattr, ec_manager_xattr,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = flags;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL,
                    "Failed to reference a "
                    "file descriptor.");

            goto out;
        }
    }
    if (dict != NULL) {
        fop->dict = dict_copy_with_ref(dict, NULL);
        if (fop->dict == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL);
    }
}

/*********************************************************************
 *
 * File Operation : fallocate
 *
 *********************************************************************/

int32_t ec_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
    return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prebuf, postbuf, xdata);
}

void ec_wind_fallocate(ec_t *ec, ec_fop_data_t *fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fallocate_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fallocate,
                      fop->fd, fop->int32, fop->offset,
                      fop->size, fop->xdata);
}

int32_t ec_manager_fallocate(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t *cbk = NULL;

    switch (state) {
    case EC_STATE_INIT:
        if (fop->size == 0) {
                ec_fop_set_error(fop, EINVAL);
                return EC_STATE_REPORT;
        }
        if (fop->int32 & (FALLOC_FL_COLLAPSE_RANGE
                         |FALLOC_FL_INSERT_RANGE
                         |FALLOC_FL_ZERO_RANGE
                         |FALLOC_FL_PUNCH_HOLE)) {
                ec_fop_set_error(fop, ENOTSUP);
                return EC_STATE_REPORT;
        }
        fop->user_size = fop->offset + fop->size;
        fop->head = ec_adjust_offset_down (fop->xl->private, &fop->offset,
                                           _gf_true);
        fop->size += fop->head;
        ec_adjust_size_up (fop->xl->private, &fop->size, _gf_true);

        /* Fall through */

    case EC_STATE_LOCK:
        ec_lock_prepare_fd(fop, fop->fd,
                           EC_UPDATE_DATA | EC_UPDATE_META | EC_QUERY_INFO,
                           fop->offset, fop->size);
        ec_lock(fop);

        return EC_STATE_DISPATCH;

    case EC_STATE_DISPATCH:

        ec_dispatch_all(fop);

        return EC_STATE_PREPARE_ANSWER;

    case EC_STATE_PREPARE_ANSWER:
        cbk = ec_fop_prepare_answer(fop, _gf_false);
        if (cbk != NULL) {
                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                            cbk->count);

                /* This shouldn't fail because we have the inode locked. */
                LOCK(&fop->locks[0].lock->loc.inode->lock);
                {
                        GF_ASSERT(__ec_get_inode_size(fop,
                                                fop->locks[0].lock->loc.inode,
                                                &cbk->iatt[0].ia_size));

                        /*If mode has FALLOC_FL_KEEP_SIZE keep the size */
                        if (fop->int32 & FALLOC_FL_KEEP_SIZE) {
                                cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
                        } else if (fop->user_size > cbk->iatt[0].ia_size) {
                                cbk->iatt[1].ia_size = fop->user_size;

                                /* This shouldn't fail because we have the inode
                                 * locked. */
                                GF_ASSERT(__ec_set_inode_size(fop,
                                          fop->locks[0].lock->loc.inode,
                                                    cbk->iatt[1].ia_size));
                        } else {
                                cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
                        }
                }
                UNLOCK(&fop->locks[0].lock->loc.inode->lock);
        }

        return EC_STATE_REPORT;

    case EC_STATE_REPORT:
        cbk = fop->answer;

        GF_ASSERT(cbk != NULL);

        if (fop->cbks.fallocate != NULL) {
                fop->cbks.fallocate(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                    cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                    cbk->xdata);
        }

        return EC_STATE_LOCK_REUSE;

    case -EC_STATE_INIT:
    case -EC_STATE_LOCK:
    case -EC_STATE_DISPATCH:
    case -EC_STATE_PREPARE_ANSWER:
    case -EC_STATE_REPORT:
        GF_ASSERT(fop->error != 0);

        if (fop->cbks.fallocate != NULL) {
                fop->cbks.fallocate(fop->req_frame, fop, fop->xl, -1,
                                    fop->error, NULL, NULL, NULL);
        }

        return EC_STATE_LOCK_REUSE;

    case -EC_STATE_LOCK_REUSE:
    case EC_STATE_LOCK_REUSE:
        ec_lock_reuse(fop);

        return EC_STATE_UNLOCK;

    case -EC_STATE_UNLOCK:
    case EC_STATE_UNLOCK:
        ec_unlock(fop);

        return EC_STATE_END;

    default:
        gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_UNHANDLED_STATE,
                "Unhandled state %d for %s",
                state, ec_fop_name(fop->id));

        return EC_STATE_END;
    }
}

void ec_fallocate(call_frame_t *frame, xlator_t *this, uintptr_t target,
              int32_t minimum, fop_fallocate_cbk_t func, void *data, fd_t *fd,
              int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
    ec_cbk_t callback = { .fallocate = func };
    ec_fop_data_t *fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FALLOCATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FALLOCATE, 0, target,
                               minimum, ec_wind_fallocate, ec_manager_fallocate,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;
    fop->int32 = mode;
    fop->offset = offset;
    fop->size = len;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_FILE_DESC_REF_FAIL,
                        "Failed to reference a "
                        "file descriptor.");
                goto out;
        }
    }

    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL,
                        "Failed to reference a "
                        "dictionary.");
                goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/*********************************************************************
 *
 * File Operation : Discard
 *
 *********************************************************************/
void ec_update_discard_write(ec_fop_data_t *fop, uintptr_t mask)
{
    ec_t   *ec       = fop->xl->private;
    off_t  off_head  = 0;
    off_t  off_tail  = 0;
    size_t size_head = 0;
    size_t size_tail = 0;
    int    error     = 0;

    off_head = fop->offset * ec->fragments - fop->int32;
    if (fop->size == 0) {
            error = ec_update_write (fop, mask, off_head, fop->user_size);
    } else {
            size_head = fop->int32;
            size_tail = (off_head + fop->user_size) % ec->stripe_size;
            off_tail = off_head + fop->user_size - size_tail;
            if (size_head) {
                    error = ec_update_write (fop, mask, off_head, size_head);
                    if (error) {
                        goto out;
                    }
            }
            if (size_tail) {
                    error = ec_update_write (fop, mask, off_tail, size_tail);
            }
    }
out:
    if (error)
            ec_fop_set_error (fop, -error);
}

void ec_discard_adjust_offset_size(ec_fop_data_t *fop)
{
        ec_t *ec = fop->xl->private;

        fop->user_size = fop->size;
        /* If discard length covers atleast a fragment on brick, we will
         * perform discard operation(when fop->size is non-zero) else we just
         * write zeros.
         */
        fop->int32 = ec_adjust_offset_up(ec, &fop->offset, _gf_true);
        fop->frag_range.first = fop->offset;
        if (fop->size < fop->int32) {
                fop->size = 0;
        } else {
                fop->size -= fop->int32;
                ec_adjust_size_down(ec, &fop->size, _gf_true);
        }
        fop->frag_range.last = fop->offset + fop->size;
}

int32_t ec_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
    return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                               prebuf, postbuf, xdata);
}

void ec_wind_discard(ec_t *ec, ec_fop_data_t *fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_discard_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->discard,
                      fop->fd, fop->offset, fop->size, fop->xdata);
}

int32_t ec_manager_discard(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t *cbk     = NULL;
    off_t         fl_start = 0;
    size_t        fl_size  = 0;


    switch (state) {
    case EC_STATE_INIT:
        if ((fop->size <= 0) || (fop->offset < 0)) {
                ec_fop_set_error(fop, EINVAL);
                return EC_STATE_REPORT;
        }
        /* Because of the head/tail writes, "discard" happens on the remaining
         * regions, but we need to compute region including head/tail writes
         * so compute them separately*/
        fl_start = fop->offset;
        fl_size = fop->size;
        fl_size += ec_adjust_offset_down (fop->xl->private, &fl_start,
                                          _gf_true);
        ec_adjust_size_up (fop->xl->private, &fl_size, _gf_true);

        ec_discard_adjust_offset_size(fop);

    /* Fall through */

    case EC_STATE_LOCK:
        ec_lock_prepare_fd(fop, fop->fd,
                           EC_UPDATE_DATA | EC_UPDATE_META |
                           EC_QUERY_INFO, fl_start, fl_size);
        ec_lock(fop);

        return EC_STATE_DISPATCH;

    case EC_STATE_DISPATCH:

        /* Dispatch discard fop only if we have whole fragment
         * to deallocate */
        if (fop->size) {
                ec_dispatch_all(fop);
                return EC_STATE_DELAYED_START;
        } else {
                /*Assume discard to have succeeded on mask*/
                fop->good = fop->mask;
        }

        /* Fall through */

    case EC_STATE_DELAYED_START:

        if (fop->size) {
                if (fop->answer && fop->answer->op_ret == 0)
                        ec_update_discard_write (fop, fop->answer->mask);
        } else {
                ec_update_discard_write (fop, fop->mask);
        }

        return EC_STATE_PREPARE_ANSWER;

    case EC_STATE_PREPARE_ANSWER:
        cbk = ec_fop_prepare_answer(fop, _gf_false);
        if (cbk != NULL) {
                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                cbk->count);

                /* This shouldn't fail because we have the inode locked. */
                GF_ASSERT(ec_get_inode_size(fop, fop->locks[0].lock->loc.inode,
                                            &cbk->iatt[0].ia_size));

                cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
        }
        return EC_STATE_REPORT;

    case EC_STATE_REPORT:
        cbk = fop->answer;

        GF_ASSERT(cbk != NULL);

        if (fop->cbks.discard != NULL) {
                fop->cbks.discard(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                  cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                  cbk->xdata);
        }

        return EC_STATE_LOCK_REUSE;

    case -EC_STATE_INIT:
    case -EC_STATE_LOCK:
    case -EC_STATE_DISPATCH:
    case -EC_STATE_DELAYED_START:
    case -EC_STATE_PREPARE_ANSWER:
    case -EC_STATE_REPORT:
        GF_ASSERT(fop->error != 0);

        if (fop->cbks.discard != NULL) {
                fop->cbks.discard(fop->req_frame, fop, fop->xl, -1,
                                  fop->error, NULL, NULL, NULL);
        }

        return EC_STATE_LOCK_REUSE;

    case -EC_STATE_LOCK_REUSE:
    case EC_STATE_LOCK_REUSE:
        ec_lock_reuse(fop);

        return EC_STATE_UNLOCK;

    case -EC_STATE_UNLOCK:
    case EC_STATE_UNLOCK:
        ec_unlock(fop);

        return EC_STATE_END;

    default:
        gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_UNHANDLED_STATE,
                "Unhandled state %d for %s",
                state, ec_fop_name(fop->id));

        return EC_STATE_END;
    }
}

void ec_discard(call_frame_t *frame, xlator_t *this, uintptr_t target,
                int32_t minimum, fop_discard_cbk_t func, void *data, fd_t *fd,
                off_t offset, size_t len, dict_t *xdata)
{
    ec_cbk_t callback = { .discard = func };
    ec_fop_data_t *fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(DISCARD) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_DISCARD, 0, target,
                               minimum, ec_wind_discard, ec_manager_discard,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;
    fop->offset = offset;
    fop->size = len;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
    }

    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/*********************************************************************
 *
 * File Operation : truncate
 *
 *********************************************************************/

int32_t ec_update_truncate_write (ec_fop_data_t *fop, uintptr_t mask)
{
        ec_t *ec = fop->xl->private;
        size_t size = fop->offset * ec->fragments - fop->user_size;
        return ec_update_write (fop, mask, fop->user_size, size);
}

int32_t ec_truncate_open_cbk(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             fd_t * fd, dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    int32_t err;

    fop->parent->good &= fop->good;
    if (op_ret >= 0) {
        fd_bind (fd);
        err = ec_update_truncate_write (fop->parent, fop->answer->mask);
        if (err != 0) {
            ec_fop_set_error (fop->parent, -err);
        }
    }

    return 0;
}

int32_t ec_truncate_clean(ec_fop_data_t * fop)
{
    if (fop->fd == NULL) {
        fop->fd = fd_create(fop->loc[0].inode, fop->frame->root->pid);
        if (fop->fd == NULL) {
            return -ENOMEM;
        }

        ec_open(fop->frame, fop->xl, fop->answer->mask, fop->minimum,
                ec_truncate_open_cbk, fop, &fop->loc[0], O_RDWR, fop->fd,
                NULL);

        return 0;
    } else {
        return ec_update_truncate_write (fop, fop->answer->mask);
    }
}

int32_t ec_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *prestat,
                         struct iatt *poststat, dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prestat, poststat, xdata);
}

void ec_wind_truncate(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_truncate_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->truncate,
                      &fop->loc[0], fop->offset, fop->xdata);
}

int32_t ec_manager_truncate(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            fop->user_size = fop->offset;
            ec_adjust_offset_up(fop->xl->private, &fop->offset, _gf_true);
            fop->frag_range.first = fop->offset;
            fop->frag_range.last = UINT64_MAX;

        /* Fall through */

        case EC_STATE_LOCK:
            if (fop->id == GF_FOP_TRUNCATE) {
                ec_lock_prepare_inode(fop, &fop->loc[0],
                                      EC_UPDATE_DATA | EC_UPDATE_META |
                                      EC_QUERY_INFO, fop->offset, LLONG_MAX);
            } else {
                ec_lock_prepare_fd(fop, fop->fd,
                                   EC_UPDATE_DATA | EC_UPDATE_META |
                                   EC_QUERY_INFO, fop->offset, LLONG_MAX);
            }
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                cbk->count);

                /* This shouldn't fail because we have the inode locked. */
                /* Inode size doesn't need to be updated under locks, because
                 * conflicting operations won't be in-flight
                 */
                GF_ASSERT(ec_get_inode_size(fop, fop->locks[0].lock->loc.inode,
                                            &cbk->iatt[0].ia_size));
                cbk->iatt[1].ia_size = fop->user_size;
                /* This shouldn't fail because we have the inode locked. */
                GF_ASSERT(ec_set_inode_size(fop, fop->locks[0].lock->loc.inode,
                                            fop->user_size));
                if ((cbk->iatt[0].ia_size > cbk->iatt[1].ia_size) &&
                    (fop->user_size != fop->offset)) {
                    err = ec_truncate_clean(fop);
                    if (err != 0) {
                        ec_cbk_set_error(cbk, -err, _gf_false);
                    }
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == GF_FOP_TRUNCATE)
            {
                if (fop->cbks.truncate != NULL)
                {
                    fop->cbks.truncate(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno,
                                       &cbk->iatt[0], &cbk->iatt[1],
                                       cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.ftruncate != NULL)
                {
                    fop->cbks.ftruncate(fop->req_frame, fop, fop->xl,
                                        cbk->op_ret, cbk->op_errno,
                                        &cbk->iatt[0], &cbk->iatt[1],
                                        cbk->xdata);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_TRUNCATE)
            {
                if (fop->cbks.truncate != NULL)
                {
                    fop->cbks.truncate(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL, NULL, NULL);
                }
            }
            else
            {
                if (fop->cbks.ftruncate != NULL)
                {
                    fop->cbks.ftruncate(fop->req_frame, fop, fop->xl, -1,
                                        fop->error, NULL, NULL, NULL);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK_REUSE:
        case EC_STATE_LOCK_REUSE:
            ec_lock_reuse(fop);

            return EC_STATE_UNLOCK;

        case -EC_STATE_UNLOCK:
        case EC_STATE_UNLOCK:
            ec_unlock(fop);

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE,
                    "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_truncate(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_truncate_cbk_t func, void * data,
                 loc_t * loc, off_t offset, dict_t * xdata)
{
    ec_cbk_t callback = { .truncate = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(TRUNCATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_TRUNCATE, 0, target,
                               minimum, ec_wind_truncate, ec_manager_truncate,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->offset = offset;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL,
                    "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/* FOP: ftruncate */

int32_t ec_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *prestat, struct iatt *poststat,
                          dict_t *xdata)
{
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prestat, poststat, xdata);
}

void ec_wind_ftruncate(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_ftruncate_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->ftruncate,
                      fop->fd, fop->offset, fop->xdata);
}

void ec_ftruncate(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_ftruncate_cbk_t func, void * data,
                  fd_t * fd, off_t offset, dict_t * xdata)
{
    ec_cbk_t callback = { .ftruncate = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FTRUNCATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FTRUNCATE, 0, target,
                               minimum, ec_wind_ftruncate, ec_manager_truncate,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->offset = offset;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL,
                    "Failed to reference a "
                    "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/* FOP: writev */
static ec_stripe_t *
ec_allocate_stripe (ec_t *ec, ec_stripe_list_t *stripe_cache)
{
        ec_stripe_t *stripe = NULL;

        if (stripe_cache->count >= stripe_cache->max) {
                GF_ASSERT (!list_empty(&stripe_cache->lru));
                stripe = list_first_entry(&stripe_cache->lru, ec_stripe_t, lru);
                list_move_tail(&stripe->lru, &stripe_cache->lru);
                GF_ATOMIC_INC(ec->stats.stripe_cache.evicts);
        } else {
                stripe = GF_MALLOC (sizeof (ec_stripe_t) + ec->stripe_size,
                                    ec_mt_ec_stripe_t);
                if (stripe != NULL) {
                        stripe_cache->count++;
                        list_add_tail (&stripe->lru, &stripe_cache->lru);
                        GF_ATOMIC_INC(ec->stats.stripe_cache.allocs);
                } else {
                        GF_ATOMIC_INC(ec->stats.stripe_cache.errors);
                }
        }

        return stripe;
}

static void
ec_write_stripe_data (ec_t *ec, ec_fop_data_t *fop,
                      ec_stripe_t *stripe)
{
        off_t   base;

        base = fop->size - ec->stripe_size;
        memcpy(stripe->data, fop->vector[0].iov_base + base, ec->stripe_size);
        stripe->frag_offset = fop->frag_range.last - ec->fragment_size;
}

static void
ec_add_stripe_in_cache (ec_t *ec, ec_fop_data_t *fop)
{
        ec_inode_t       *ctx    = NULL;
        ec_stripe_t      *stripe = NULL;
        ec_stripe_list_t *stripe_cache = NULL;
        gf_boolean_t      failed = _gf_true;

        LOCK(&fop->fd->inode->lock);

        ctx = __ec_inode_get (fop->fd->inode, fop->xl);
        if (ctx == NULL) {
                goto out;
        }

        stripe_cache = &ctx->stripe_cache;
        if (stripe_cache->max > 0) {
                stripe = ec_allocate_stripe (ec, stripe_cache);
                if (stripe == NULL) {
                        goto out;
                }

                ec_write_stripe_data (ec, fop, stripe);
        }

        failed = _gf_false;

out:
        UNLOCK(&fop->fd->inode->lock);

        if (failed) {
                gf_msg (ec->xl->name, GF_LOG_DEBUG, ENOMEM,
                        EC_MSG_FILE_DESC_REF_FAIL,
                        "Failed to create and add stripe in cache");
        }
}

int32_t ec_writev_merge_tail(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             struct iovec * vector, int32_t count,
                             struct iatt * stbuf, struct iobref * iobref,
                             dict_t * xdata)
{
    ec_t * ec = this->private;
    ec_fop_data_t * fop = frame->local;
    size_t size, base, tmp;

    if (op_ret >= 0)
    {
        tmp = 0;
        size = fop->size - fop->user_size - fop->head;
        base = ec->stripe_size - size;
        if (op_ret > base)
        {
            tmp = min(op_ret - base, size);
            ec_iov_copy_to(fop->vector[0].iov_base + fop->size - size, vector,
                           count, base, tmp);

            size -= tmp;
        }

        if (size > 0)
        {
            memset(fop->vector[0].iov_base + fop->size - size, 0, size);
        }

        if (ec->stripe_cache) {
                ec_add_stripe_in_cache (ec, fop);
        }
    }
    return 0;
}

int32_t ec_writev_merge_head(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             struct iovec * vector, int32_t count,
                             struct iatt * stbuf, struct iobref * iobref,
                             dict_t * xdata)
{
    ec_t * ec = this->private;
    ec_fop_data_t * fop = frame->local;
    size_t size, base;

    if (op_ret >= 0)
    {
        size = fop->head;
        base = 0;

        if (op_ret > 0)
        {
            base = min(op_ret, size);
            ec_iov_copy_to(fop->vector[0].iov_base, vector, count, 0, base);

            size -= base;
        }

        if (size > 0)
        {
            memset(fop->vector[0].iov_base + base, 0, size);
        }

        size = fop->size - fop->user_size - fop->head;
        if ((size > 0) && (fop->size == ec->stripe_size))
        {
            ec_writev_merge_tail(frame, cookie, this, op_ret, op_errno, vector,
                                 count, stbuf, iobref, xdata);
        }
    }

    return 0;
}

static int
ec_make_internal_fop_xdata (dict_t **xdata)
{
    dict_t *dict = NULL;

    if (*xdata)
        return 0;

    dict = dict_new();
    if (!dict)
       goto out;

    if (dict_set_str (dict, GLUSTERFS_INTERNAL_FOP_KEY, "yes"))
       goto out;

    *xdata = dict;
    return 0;
out:
    if (dict)
            dict_unref (dict);
    return -1;
}

static int32_t
ec_writev_prepare_buffers(ec_t *ec, ec_fop_data_t *fop)
{
    struct iobref *iobref = NULL;
    struct iovec *iov;
    void *ptr;
    int32_t err;

    fop->user_size = iov_length(fop->vector, fop->int32);
    fop->head = ec_adjust_offset_down(ec, &fop->offset, _gf_false);
    fop->frag_range.first = fop->offset / ec->fragments;
    fop->size = fop->user_size + fop->head;
    ec_adjust_size_up(ec, &fop->size, _gf_false);
    fop->frag_range.last = fop->frag_range.first + fop->size / ec->fragments;

    if ((fop->int32 != 1) || (fop->head != 0) ||
        (fop->size > fop->user_size) ||
        !EC_ALIGN_CHECK(fop->vector[0].iov_base, EC_METHOD_WORD_SIZE)) {
        err = ec_buffer_alloc(ec->xl, fop->size, &iobref, &ptr);
        if (err != 0) {
            goto out;
        }

        ec_iov_copy_to(ptr + fop->head, fop->vector, fop->int32, 0,
                       fop->user_size);

        fop->vector[0].iov_base = ptr;
        fop->vector[0].iov_len = fop->size;

        iobref_unref(fop->buffers);
        fop->buffers = iobref;
    }

    if (fop->int32 != 2) {
        iov = GF_MALLOC(VECTORSIZE(2), gf_common_mt_iovec);
        if (iov == NULL) {
            err = -ENOMEM;

            goto out;
        }
        iov[0].iov_base = fop->vector[0].iov_base;
        iov[0].iov_len = fop->vector[0].iov_len;

        GF_FREE(fop->vector);
        fop->vector = iov;
    }

    fop->vector[1].iov_len = fop->size / ec->fragments;
    err = ec_buffer_alloc(ec->xl, fop->vector[1].iov_len * ec->nodes,
                          &fop->buffers, &fop->vector[1].iov_base);
    if (err != 0) {
        goto out;
    }

    err = 0;

out:
    return err;
}

static void
ec_merge_stripe_head_locked (ec_t *ec, ec_fop_data_t *fop, ec_stripe_t *stripe)
{
        size_t head, size;

        head = fop->head;
        memcpy(fop->vector[0].iov_base, stripe->data, head);

        size = ec->stripe_size - head;
        if (size > fop->user_size) {
                head += fop->user_size;
                size = ec->stripe_size - head;
                memcpy(fop->vector[0].iov_base + head, stripe->data + head,
                       size);
        }
}

static void
ec_merge_stripe_tail_locked (ec_t *ec, ec_fop_data_t *fop, ec_stripe_t *stripe)
{
        size_t head, tail;
        off_t  offset;

        offset = fop->user_size + fop->head;
        tail = fop->size - offset;
        head = ec->stripe_size - tail;

        memcpy(fop->vector[0].iov_base + offset, stripe->data + head, tail);
}

static ec_stripe_t *
ec_get_stripe_from_cache_locked (ec_t *ec, ec_fop_data_t *fop,
                                 uint64_t frag_offset)
{
        ec_inode_t       *ctx    = NULL;
        ec_stripe_t      *stripe = NULL;
        ec_stripe_list_t *stripe_cache = NULL;

        ctx = __ec_inode_get (fop->fd->inode, fop->xl);
        if (ctx == NULL) {
                GF_ATOMIC_INC(ec->stats.stripe_cache.errors);
                return NULL;
        }

        stripe_cache = &ctx->stripe_cache;
        list_for_each_entry (stripe, &stripe_cache->lru, lru) {
                if (stripe->frag_offset == frag_offset) {
                        list_move_tail (&stripe->lru, &stripe_cache->lru);
                        GF_ATOMIC_INC(ec->stats.stripe_cache.hits);
                        return stripe;
                }
        }

        GF_ATOMIC_INC(ec->stats.stripe_cache.misses);

        return NULL;
}

static gf_boolean_t
ec_get_and_merge_stripe (ec_t *ec, ec_fop_data_t *fop, ec_stripe_part_t which)
{
        uint64_t      frag_offset;
        ec_stripe_t  *stripe = NULL;
        gf_boolean_t  found  = _gf_false;

        if (!ec->stripe_cache) {
                return found;
        }

        LOCK(&fop->fd->inode->lock);
        if (which == EC_STRIPE_HEAD) {
                frag_offset = fop->frag_range.first;
                stripe = ec_get_stripe_from_cache_locked(ec, fop, frag_offset);
                if (stripe) {
                        ec_merge_stripe_head_locked (ec, fop, stripe);
                        found = _gf_true;
                }
        }

        if (which == EC_STRIPE_TAIL) {
                frag_offset = fop->frag_range.last - ec->fragment_size;
                stripe = ec_get_stripe_from_cache_locked(ec, fop, frag_offset);
                if (stripe) {
                        ec_merge_stripe_tail_locked (ec, fop, stripe);
                        found = _gf_true;
                }
        }
        UNLOCK(&fop->fd->inode->lock);

        return found;
}

void ec_writev_start(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;
    ec_fd_t *ctx;
    fd_t *fd;
    dict_t *xdata = NULL;
    uint64_t tail, current;
    int32_t err = -ENOMEM;
    gf_boolean_t found_stripe = _gf_false;

    /* This shouldn't fail because we have the inode locked. */
    GF_ASSERT(ec_get_inode_size(fop, fop->fd->inode, &current));

    fd = fd_anonymous(fop->fd->inode);
    if (fd == NULL) {
        goto failed;
    }

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    ctx = ec_fd_get(fop->fd, fop->xl);
    if (ctx != NULL) {
        if ((ctx->flags & O_APPEND) != 0) {
            /* Appending writes take full locks so size won't change because
             * of any parallel operations
             */
            fop->offset = current;
        }
    }

    err = ec_writev_prepare_buffers(ec, fop);
    if (err != 0) {
        goto failed_fd;
    }
    if (fop->head > 0) {
        found_stripe = ec_get_and_merge_stripe (ec, fop, EC_STRIPE_HEAD);
        if (!found_stripe) {
                if (ec_make_internal_fop_xdata (&xdata)) {
                        err = -ENOMEM;
                        goto failed_xdata;
                }
                ec_readv(fop->frame, fop->xl, -1, EC_MINIMUM_MIN,
                         ec_writev_merge_head,
                         NULL, fd, ec->stripe_size, fop->offset, 0, xdata);
        }
    }

    tail = fop->size - fop->user_size - fop->head;
    if ((tail > 0) && ((fop->head == 0) || (fop->size > ec->stripe_size))) {
            /* Current locking scheme will make sure the 'current' below will
             * never decrease while the fop is in progress, so the checks will
             * work as expected
             */
        if (current > fop->offset + fop->head + fop->user_size) {
            found_stripe = ec_get_and_merge_stripe (ec, fop, EC_STRIPE_TAIL);
            if (!found_stripe) {
                    if (ec_make_internal_fop_xdata (&xdata)) {
                            err = -ENOMEM;
                            goto failed_xdata;
                    }
                    ec_readv(fop->frame, fop->xl, -1, EC_MINIMUM_MIN,
                             ec_writev_merge_tail, NULL, fd, ec->stripe_size,
                             fop->offset + fop->size - ec->stripe_size,
                             0, xdata);
            }
        } else {
            memset(fop->vector[0].iov_base + fop->size - tail, 0, tail);
            if (ec->stripe_cache) {
                ec_add_stripe_in_cache (ec, fop);
            }
        }
    }

    err = 0;

failed_xdata:
    if (xdata) {
        dict_unref(xdata);
    }
failed_fd:
    fd_unref(fd);
failed:
    ec_fop_set_error(fop, -err);
}

int32_t ec_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prestat,
                       struct iatt *poststat, dict_t *xdata)
{
        ec_t    *ec = NULL;
        if (this && this->private) {
                ec = this->private;
                if ((op_ret > 0) && ((op_ret % ec->fragment_size) != 0)) {
                        op_ret = -1;
                        op_errno = EIO;
                }
        }
        return ec_inode_write_cbk (frame, this, cookie, op_ret, op_errno,
                                   prestat, poststat, xdata);
}

void ec_wind_writev(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    struct iovec vector[1];
    size_t size;

    size = fop->vector[1].iov_len;

    vector[0].iov_base = fop->vector[1].iov_base + idx * size;
    vector[0].iov_len = size;

    STACK_WIND_COOKIE(fop->frame, ec_writev_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->writev,
                      fop->fd, vector, 1, fop->offset / ec->fragments,
                      fop->uint32, fop->buffers, fop->xdata);
}

static void
ec_writev_encode(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;
    void *blocks[ec->nodes];
    uint32_t i;

    blocks[0] = fop->vector[1].iov_base;
    for (i = 1; i < ec->nodes; i++) {
        blocks[i] = blocks[i - 1] + fop->vector[1].iov_len;
    }
    ec_method_encode(&ec->matrix, fop->vector[0].iov_len,
                     fop->vector[0].iov_base, blocks);
}

int32_t ec_manager_writev(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t *cbk;
    ec_fd_t *ctx = NULL;
    ec_t    *ec = fop->xl->private;
    off_t fl_start = 0;
    size_t fl_size = LLONG_MAX;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
                ctx = ec_fd_get(fop->fd, fop->xl);
                if (ctx != NULL) {
                    if ((ctx->flags & O_APPEND) == 0) {
                            off_t user_size = 0;
                            off_t head = 0;

                            fl_start = fop->offset;
                            user_size = iov_length(fop->vector, fop->int32);
                            head = ec_adjust_offset_down(ec, &fl_start,
                                                         _gf_true);
                            fl_size = user_size + head;
                            ec_adjust_size_up(ec, &fl_size, _gf_true);
                    }
                }
            ec_lock_prepare_fd(fop, fop->fd,
                               EC_UPDATE_DATA | EC_UPDATE_META |
                               EC_QUERY_INFO, fl_start, fl_size);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_writev_start(fop);

            return EC_STATE_DELAYED_START;

        case EC_STATE_DELAYED_START:
            /* Restore uid, gid if they were changed to do some partial
             * reads. */
            fop->frame->root->uid = fop->uid;
            fop->frame->root->gid = fop->gid;

            ec_writev_encode(fop);

            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                ec_t *ec = fop->xl->private;
                size_t size;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                cbk->count);

                /* This shouldn't fail because we have the inode locked. */
                LOCK(&fop->fd->inode->lock);
                {
                        GF_ASSERT(__ec_get_inode_size(fop, fop->fd->inode,
                                                    &cbk->iatt[0].ia_size));
                        cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
                        size = fop->offset + fop->head + fop->user_size;
                        if (size > cbk->iatt[0].ia_size) {
                           /* Only update inode size if this is a top level fop.
                            * Otherwise this is an internal write and the top
                            * level fop should take care of the real inode size.
                            */
                            if (fop->parent == NULL) {
                                /* This shouldn't fail because we have the inode
                                 * locked. */
                                GF_ASSERT(__ec_set_inode_size(fop,
                                                        fop->fd->inode, size));
                            }
                            cbk->iatt[1].ia_size = size;
                        }
                }
                UNLOCK(&fop->fd->inode->lock);

                if (fop->error == 0) {
                    cbk->op_ret *= ec->fragments;
                    if (cbk->op_ret < fop->head) {
                        cbk->op_ret = 0;
                    } else {
                        cbk->op_ret -= fop->head;
                    }
                    if (cbk->op_ret > fop->user_size) {
                        cbk->op_ret = fop->user_size;
                    }
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.writev != NULL)
            {
                fop->cbks.writev(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                 cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                 cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_DELAYED_START:
            /* We have failed while doing partial reads. We need to restore
             * original uid, gid. */
            fop->frame->root->uid = fop->uid;
            fop->frame->root->gid = fop->gid;

        /* Fall through */

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.writev != NULL)
            {
                fop->cbks.writev(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL, NULL, NULL);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK_REUSE:
        case EC_STATE_LOCK_REUSE:
            ec_lock_reuse(fop);

            return EC_STATE_UNLOCK;

        case -EC_STATE_UNLOCK:
        case EC_STATE_UNLOCK:
            ec_unlock(fop);

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE,
                    "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_writev(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_writev_cbk_t func, void * data, fd_t * fd,
               struct iovec * vector, int32_t count, off_t offset,
               uint32_t flags, struct iobref * iobref, dict_t * xdata)
{
    ec_cbk_t callback = { .writev = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(WRITE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_WRITE, 0, target, minimum,
                               ec_wind_writev, ec_manager_writev, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = count;
    fop->offset = offset;
    fop->uint32 = flags;

    fop->use_fd = 1;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL,
                    "Failed to reference a "
                    "file descriptor.");

            goto out;
        }
    }
    if (count > 0) {
        fop->vector = iov_dup(vector, count);
        if (fop->vector == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_NO_MEMORY,
                    "Failed to duplicate a "
                    "vector list.");

            goto out;
        }
        fop->int32 = count;
    }
    if (iobref != NULL) {
        fop->buffers = iobref_ref(iobref);
        if (fop->buffers == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_BUF_REF_FAIL,
                    "Failed to reference a "
                    "buffer.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL,
                    "Failed to reference a "
                    "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}
