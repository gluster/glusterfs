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
#include "byte-order.h"

#include "ec.h"
#include "ec-messages.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-combine.h"
#include "ec-method.h"
#include "ec-fops.h"

/* FOP: flush */

int32_t ec_flush_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FLUSH, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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

void ec_wind_flush(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_flush_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->flush, fop->fd,
                      fop->xdata);
}

int32_t ec_manager_flush(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_fd(fop, fop->fd, 0);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_flush_size_version(fop);

            return EC_STATE_DELAYED_START;

        case EC_STATE_DELAYED_START:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            ec_fop_prepare_answer(fop, _gf_false);

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.flush != NULL)
            {
                fop->cbks.flush(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DELAYED_START:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.flush != NULL)
            {
                fop->cbks.flush(fop->req_frame, fop, fop->xl, -1, fop->error,
                                NULL);
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
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_flush(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_flush_cbk_t func, void * data, fd_t * fd,
              dict_t * xdata)
{
    ec_cbk_t callback = { .flush = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FLUSH) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FLUSH, 0, target, minimum,
                               ec_wind_flush, ec_manager_flush, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: fsync */

int32_t ec_combine_fsync(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(fop, dst->iatt, src->iatt, 2)) {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_IATT_MISMATCH, "Mismatching iatt in "
                                             "answers of 'GF_FOP_FSYNC'");

        return 0;
    }

    return 1;
}

int32_t ec_fsync_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iatt * prebuf,
                     struct iatt * postbuf, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSYNC, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (prebuf != NULL)
            {
                cbk->iatt[0] = *prebuf;
            }
            if (postbuf != NULL)
            {
                cbk->iatt[1] = *postbuf;
            }
        }
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
        }

        ec_combine(cbk, ec_combine_fsync);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fsync(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fsync_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fsync, fop->fd,
                      fop->int32, fop->xdata);
}

int32_t ec_manager_fsync(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_flush_size_version(fop);

            return EC_STATE_DELAYED_START;

        case EC_STATE_DELAYED_START:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                cbk->count);

                /* This shouldn't fail because we have the inode locked. */
                GF_ASSERT(ec_get_inode_size(fop, fop->fd->inode,
                                            &cbk->iatt[0].ia_size));
                cbk->iatt[1].ia_size = cbk->iatt[0].ia_size;
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.fsync != NULL)
            {
                fop->cbks.fsync(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
        case -EC_STATE_DELAYED_START:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.fsync != NULL)
            {
                fop->cbks.fsync(fop->req_frame, fop, fop->xl, -1, fop->error,
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
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_fsync(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fsync_cbk_t func, void * data, fd_t * fd,
              int32_t datasync, dict_t * xdata)
{
    ec_cbk_t callback = { .fsync = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FSYNC) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSYNC, 0, target, minimum,
                               ec_wind_fsync, ec_manager_fsync, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = datasync;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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

/* FOP: fsyncdir */

int32_t ec_fsyncdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSYNCDIR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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

void ec_wind_fsyncdir(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fsyncdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fsyncdir,
                      fop->fd, fop->int32, fop->xdata);
}

int32_t ec_manager_fsyncdir(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_fd(fop, fop->fd, 0);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_flush_size_version(fop);

            return EC_STATE_DELAYED_START;

        case EC_STATE_DELAYED_START:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            ec_fop_prepare_answer(fop, _gf_false);

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.fsyncdir != NULL)
            {
                fop->cbks.fsyncdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                   cbk->op_errno, cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
        case -EC_STATE_DELAYED_START:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.fsyncdir != NULL)
            {
                fop->cbks.fsyncdir(fop->req_frame, fop, fop->xl, -1,
                                   fop->error, NULL);
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
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_fsyncdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fsyncdir_cbk_t func, void * data,
                 fd_t * fd, int32_t datasync, dict_t * xdata)
{
    ec_cbk_t callback = { .fsyncdir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FSYNCDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSYNCDIR, 0, target,
                               minimum, ec_wind_fsyncdir, ec_manager_fsyncdir,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = datasync;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: lookup */

void ec_lookup_rebuild(ec_t * ec, ec_fop_data_t * fop, ec_cbk_data_t * cbk)
{
    ec_inode_t * ctx = NULL;
    uint64_t size = 0;
    int32_t have_size = 0, err;

    if (cbk->op_ret < 0) {
        return;
    }

    ec_dict_del_array(cbk->xdata, EC_XATTR_VERSION, cbk->version,
                      EC_VERSION_SIZE);

    err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode, &cbk->iatt[0]);
    if (ec_cbk_set_error(cbk, -err, _gf_true)) {
        return;
    }

    LOCK(&cbk->inode->lock);

    ctx = __ec_inode_get(cbk->inode, fop->xl);
    if (ctx != NULL)
    {
        if (ctx->have_version) {
            cbk->version[0] = ctx->post_version[0];
            cbk->version[1] = ctx->post_version[1];
        }
        if (ctx->have_size) {
            size = ctx->post_size;
            have_size = 1;
        }
    }

    UNLOCK(&cbk->inode->lock);

    if (cbk->iatt[0].ia_type == IA_IFREG)
    {
        cbk->size = cbk->iatt[0].ia_size;
        ec_dict_del_number(cbk->xdata, EC_XATTR_SIZE, &cbk->iatt[0].ia_size);
        if (have_size)
        {
            cbk->iatt[0].ia_size = size;
        }
    }
}

int32_t ec_combine_lookup(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(fop, dst->iatt, src->iatt, 2)) {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_IATT_MISMATCH, "Mismatching iatt in "
                                             "answers of 'GF_FOP_LOOKUP'");

        return 0;
    }

    return 1;
}

int32_t ec_lookup_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, inode_t * inode,
                      struct iatt * buf, dict_t * xdata,
                      struct iatt * postparent)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_LOOKUP, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (inode != NULL)
            {
                cbk->inode = inode_ref(inode);
                if (cbk->inode == NULL)
                {
                    gf_msg (this->name, GF_LOG_ERROR, 0,
                            EC_MSG_INODE_REF_FAIL,
                            "Failed to reference an inode.");

                    goto out;
                }
            }
            if (buf != NULL)
            {
                cbk->iatt[0] = *buf;
            }
            if (postparent != NULL)
            {
                cbk->iatt[1] = *postparent;
            }
        }
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
            ec_dict_del_array (xdata, EC_XATTR_DIRTY, cbk->dirty,
                               EC_VERSION_SIZE);
        }

        ec_combine(cbk, ec_combine_lookup);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_lookup(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_lookup_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->lookup,
                      &fop->loc[0], fop->xdata);
}

int32_t ec_manager_lookup(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t *cbk;
    int32_t err;

    switch (state)
    {
        case EC_STATE_INIT:
            if (fop->xdata == NULL) {
                fop->xdata = dict_new();
                if (fop->xdata == NULL) {
                    gf_msg (fop->xl->name, GF_LOG_ERROR, ENOMEM,
                            EC_MSG_LOOKUP_REQ_PREP_FAIL, "Unable to prepare "
                            "lookup request");

                    fop->error = ENOMEM;

                    return EC_STATE_REPORT;
                }
            } else {
                /*TODO: To be handled once we have 'syndromes' */
                dict_del (fop->xdata, GF_CONTENT_KEY);
            }
            err = dict_set_uint64(fop->xdata, EC_XATTR_SIZE, 0);
            if (err == 0) {
                err = dict_set_uint64(fop->xdata, EC_XATTR_VERSION, 0);
            }
            if (err == 0) {
                err = dict_set_uint64(fop->xdata, EC_XATTR_DIRTY, 0);
            }
            if (err != 0) {
                gf_msg (fop->xl->name, GF_LOG_ERROR, -err,
                        EC_MSG_LOOKUP_REQ_PREP_FAIL, "Unable to prepare lookup "
                                                    "request");

                fop->error = -err;

                return EC_STATE_REPORT;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            /*
             * Lookup happens without any lock, so there is a chance that it
             * will have answers before modification happened and after
             * modification happened in the same response. So choose the next
             * best answer when the answers don't match for EC_MINIMUM_MIN
             */

            if (!fop->answer && !list_empty(&fop->cbk_list)) {
                fop->answer = list_entry (fop->cbk_list.next, ec_cbk_data_t,
                                          list);
            }

            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2, cbk->count);

                ec_lookup_rebuild(fop->xl->private, fop, cbk);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.lookup != NULL)
            {
                fop->cbks.lookup(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                 cbk->op_errno, cbk->inode, &cbk->iatt[0],
                                 cbk->xdata, &cbk->iatt[1]);
            }

            return EC_STATE_END;

        case -EC_STATE_INIT:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.lookup != NULL)
            {
                fop->cbks.lookup(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL, NULL, NULL, NULL);
            }

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_lookup(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_lookup_cbk_t func, void * data,
               loc_t * loc, dict_t * xdata)
{
    ec_cbk_t callback = { .lookup = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(LOOKUP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_LOOKUP, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_lookup,
                               ec_manager_lookup, callback, data);
    if (fop == NULL) {
        goto out;
    }

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
        /* Do not log failures here as a memory problem would have already
         * been logged by the corresponding alloc functions */
        if (fop->xdata == NULL)
            goto out;
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL);
    }
}

/* FOP: statfs */

int32_t ec_combine_statfs(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    ec_statvfs_combine(&dst->statvfs, &src->statvfs);

    return 1;
}

int32_t ec_statfs_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, struct statvfs * buf,
                      dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_STATFS, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (buf != NULL)
            {
                cbk->statvfs = *buf;
            }
        }
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
        }

        ec_combine(cbk, ec_combine_statfs);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_statfs(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_statfs_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->statfs,
                      &fop->loc[0], fop->xdata);
}

int32_t ec_manager_statfs(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t   *cbk                   = NULL;
    gf_boolean_t     deem_statfs_enabled   = _gf_false;
    int32_t          err                   = 0;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                ec_t *ec = fop->xl->private;

                if (cbk->xdata) {
                    err = dict_get_int8 (cbk->xdata, "quota-deem-statfs",
                                          (int8_t *)&deem_statfs_enabled);
                    if (err != -ENOENT) {
                        ec_cbk_set_error(cbk, -err, _gf_true);
                    }
                }

                if (err != 0 || deem_statfs_enabled == _gf_false) {
                    cbk->statvfs.f_blocks *= ec->fragments;
                    cbk->statvfs.f_bfree *= ec->fragments;
                    cbk->statvfs.f_bavail *= ec->fragments;
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.statfs != NULL)
            {
                fop->cbks.statfs(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                 cbk->op_errno, &cbk->statvfs, cbk->xdata);
            }

            return EC_STATE_END;

        case -EC_STATE_INIT:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.statfs != NULL)
            {
                fop->cbks.statfs(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL, NULL);
            }

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_statfs(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_statfs_cbk_t func, void * data,
               loc_t * loc, dict_t * xdata)
{
    ec_cbk_t callback = { .statfs = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(STATFS) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_STATFS, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_statfs,
                               ec_manager_statfs, callback, data);
    if (fop == NULL) {
        goto out;
    }

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}

/* FOP: xattrop */

int32_t ec_combine_xattrop(ec_fop_data_t *fop, ec_cbk_data_t *dst,
                           ec_cbk_data_t *src)
{
    if (!ec_dict_compare(dst->dict, src->dict))
    {
        gf_msg (fop->xl->name, GF_LOG_DEBUG, 0,
                EC_MSG_DICT_MISMATCH, "Mismatching dictionary in "
                                             "answers of 'GF_FOP_XATTROP'");

        return 0;
    }

    return 1;
}

int32_t
ec_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xattr,
                dict_t *xdata)
{
        ec_fop_data_t *fop = NULL;
        ec_cbk_data_t *cbk = NULL;
        data_t *data;
        uint64_t *version;
        int32_t idx = (int32_t)(uintptr_t)cookie;

        VALIDATE_OR_GOTO (this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, frame->local, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        fop = frame->local;

        ec_trace ("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
                  frame, op_ret, op_errno);

        cbk = ec_cbk_data_allocate (frame, this, fop, fop->id, idx, op_ret,
                                    op_errno);
        if (!cbk)
                goto out;

        if (op_ret >= 0) {
                cbk->dict = dict_ref (xattr);

                data = dict_get(cbk->dict, EC_XATTR_VERSION);
                if ((data != NULL) && (data->len >= sizeof(uint64_t))) {
                    version = (uint64_t *)data->data;

                    if (((ntoh64(version[0]) >> EC_SELFHEAL_BIT) & 1) != 0) {
                            LOCK(&fop->lock);

                            fop->healing |= 1ULL << idx;

                            UNLOCK(&fop->lock);
                    }
                }

                ec_dict_del_array (xattr, EC_XATTR_DIRTY, cbk->dirty,
                                   EC_VERSION_SIZE);
        }

        if (xdata)
                cbk->xdata = dict_ref(xdata);

        ec_combine (cbk, ec_combine_xattrop);

out:
        if (fop)
            ec_complete(fop);

        return 0;
}

void ec_wind_xattrop(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_xattrop_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->xattrop,
                      &fop->loc[0], fop->xattrop_flags, fop->dict, fop->xdata);
}

int32_t ec_manager_xattrop(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            if (fop->fd == NULL) {
                ec_lock_prepare_inode(fop, &fop->loc[0], EC_UPDATE_META);
            } else {
                ec_lock_prepare_fd(fop, fop->fd, EC_UPDATE_META);
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

                err = ec_dict_combine(cbk, EC_COMBINE_DICT);
                ec_cbk_set_error(cbk, -err, _gf_false);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == GF_FOP_XATTROP)
            {
                if (fop->cbks.xattrop != NULL)
                {
                    fop->cbks.xattrop(fop->req_frame, fop, fop->xl,
                                      cbk->op_ret, cbk->op_errno, cbk->dict,
                                      cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fxattrop != NULL)
                {
                    fop->cbks.fxattrop(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno, cbk->dict,
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

            if (fop->id == GF_FOP_XATTROP)
            {
                if (fop->cbks.xattrop != NULL)
                {
                    fop->cbks.xattrop(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL, NULL);
                }
            }
            else
            {
                if (fop->cbks.fxattrop != NULL)
                {
                    fop->cbks.fxattrop(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL, NULL);
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
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_xattrop(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_xattrop_cbk_t func, void * data,
                loc_t * loc, gf_xattrop_flags_t optype, dict_t * xattr,
                dict_t * xdata)
{
    ec_cbk_t callback = { .xattrop = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(XATTROP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_XATTROP, 0, target, minimum,
                               ec_wind_xattrop, ec_manager_xattrop, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->xattrop_flags = optype;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xattr != NULL) {
        fop->dict = dict_ref(xattr);
        if (fop->dict == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}

void ec_wind_fxattrop(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_xattrop_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fxattrop,
                      fop->fd, fop->xattrop_flags, fop->dict, fop->xdata);
}

void ec_fxattrop(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fxattrop_cbk_t func, void * data,
                 fd_t * fd, gf_xattrop_flags_t optype, dict_t * xattr,
                 dict_t * xdata)
{
    ec_cbk_t callback = { .fxattrop = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FXATTROP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FXATTROP, 0, target,
                               minimum, ec_wind_fxattrop, ec_manager_xattrop,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->xattrop_flags = optype;

    if (fd != NULL) {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_FILE_DESC_REF_FAIL, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xattr != NULL) {
        fop->dict = dict_ref(xattr);
        if (fop->dict == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}

/* FOP: IPC */

int32_t ec_ipc_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                   int32_t op_ret, int32_t op_errno, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_IPC, idx, op_ret,
                               op_errno);

    if (cbk != NULL)
    {
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
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

void ec_wind_ipc(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_ipc_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->ipc,
                      fop->int32, fop->xdata);
}

int32_t ec_manager_ipc(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            ec_fop_prepare_answer(fop, _gf_true);

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);
            if (fop->cbks.ipc != NULL)
            {
                fop->cbks.ipc(fop->req_frame, fop, fop->xl, cbk->op_ret,
                               cbk->op_errno, cbk->xdata);
            }

            return EC_STATE_END;

        case -EC_STATE_INIT:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.ipc != NULL)
            {
                fop->cbks.ipc(fop->req_frame, fop, fop->xl, -1, fop->error,
                              NULL);
            }

            return EC_STATE_END;

        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_ipc(call_frame_t *frame, xlator_t *this, uintptr_t target,
            int32_t minimum, fop_ipc_cbk_t func, void *data, int32_t op,
            dict_t *xdata)
{
    ec_cbk_t callback = { .ipc = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(IPC) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_IPC, 0, target, minimum,
                               ec_wind_ipc, ec_manager_ipc, callback, data);
    if (fop == NULL) {
        goto out;
    }
    if (xdata != NULL) {
        fop->xdata = dict_ref(xdata);
    }
    fop->int32 = op;

    error = 0;

out:
    if (fop != NULL) {
        ec_manager(fop, error);
    } else {
        func(frame, NULL, this, -1, error, NULL);
    }
}
