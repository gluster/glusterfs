/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "xlator.h"
#include "defaults.h"

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
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
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
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
            }

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

        case -EC_STATE_LOCK:
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FLUSH) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FLUSH, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_flush,
                               ec_manager_flush, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    if (fd != NULL)
    {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL);
    }
}

/* FOP: fsync */

int32_t ec_combine_fsync(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
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
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
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
            ec_lock_prepare_fd(fop, fop->fd, 0);
            ec_lock(fop);

            return EC_STATE_GET_SIZE_AND_VERSION;

        case EC_STATE_GET_SIZE_AND_VERSION:
            ec_get_size_version(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_flush_size_version(fop);

            return EC_STATE_DELAYED_START;

        case EC_STATE_DELAYED_START:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                    cbk->count);

                    cbk->iatt[0].ia_size = fop->pre_size;
                    cbk->iatt[1].ia_size = fop->post_size;
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
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

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FSYNC) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSYNC, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_fsync,
                               ec_manager_fsync, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = datasync;

    if (fd != NULL)
    {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL);
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
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
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
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
            }

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

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FSYNCDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSYNCDIR, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_fsyncdir,
                               ec_manager_fsyncdir, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = datasync;

    if (fd != NULL)
    {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL);
    }
}

/* FOP: lookup */

void ec_lookup_rebuild(ec_t * ec, ec_fop_data_t * fop, ec_cbk_data_t * cbk)
{
    ec_cbk_data_t * ans = NULL;
    ec_inode_t * ctx = NULL;
    ec_lock_t * lock = NULL;
    data_t * data = NULL;
    uint8_t * buff = NULL;
    uint64_t size = 0;
    int32_t i = 0, have_size = 0;

    if (cbk->op_ret < 0)
    {
        return;
    }

    ec_dict_del_number(cbk->xdata, EC_XATTR_VERSION, &cbk->version);

    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode, &cbk->iatt[0]);

    LOCK(&cbk->inode->lock);

    ctx = __ec_inode_get(cbk->inode, fop->xl);
    if ((ctx != NULL) && (ctx->inode_lock != NULL))
    {
        lock = ctx->inode_lock;
        cbk->version = lock->version;
        if (lock->have_size)
        {
            size = lock->size;
            have_size = 1;
        }
    }

    UNLOCK(&cbk->inode->lock);

    if (cbk->iatt[0].ia_type == IA_IFREG)
    {
        uint8_t * blocks[cbk->count];
        uint32_t values[cbk->count];

        cbk->size = cbk->iatt[0].ia_size;
        ec_dict_del_number(cbk->xdata, EC_XATTR_SIZE, &cbk->iatt[0].ia_size);
        if (have_size)
        {
            cbk->iatt[0].ia_size = size;
        }

        size = UINT64_MAX;
        for (i = 0, ans = cbk; (ans != NULL) && (i < ec->fragments);
             ans = ans->next)
        {
            data = dict_get(ans->xdata, GF_CONTENT_KEY);
            if (data != NULL)
            {
                values[i] = ans->idx;
                blocks[i] = (uint8_t *)data->data;
                if (size > data->len)
                {
                    size = data->len;
                }
                i++;
            }
        }

        if (i >= ec->fragments)
        {
            size -= size % ec->fragment_size;
            if (size > 0)
            {
                buff = GF_MALLOC(size * ec->fragments, gf_common_mt_char);
                if (buff != NULL)
                {
                    size = ec_method_decode(size, ec->fragments, values,
                                            blocks, buff);
                    if (size > fop->size)
                    {
                        size = fop->size;
                    }
                    if (size > cbk->iatt[0].ia_size)
                    {
                        size = cbk->iatt[0].ia_size;
                    }

                    if (dict_set_bin(cbk->xdata, GF_CONTENT_KEY, buff,
                                     size) != 0)
                    {
                        GF_FREE(buff);
                        buff = NULL;
                        gf_log(fop->xl->name, GF_LOG_WARNING, "Lookup "
                                                              "read-ahead "
                                                              "failed");
                    }
                }
                else
                {
                    gf_log(fop->xl->name, GF_LOG_WARNING, "Lookup read-ahead "
                                                          "failed");
                }
            }
        }

        if (buff == NULL)
        {
            dict_del(cbk->xdata, GF_CONTENT_KEY);
        }
    }
}

int32_t ec_combine_lookup(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
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
                    gf_log(this->name, GF_LOG_ERROR,
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
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
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
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            if (fop->xdata == NULL)
            {
                fop->xdata = dict_new();
                if (fop->xdata == NULL)
                {
                    gf_log(fop->xl->name, GF_LOG_ERROR, "Unable to prepare "
                                                        "lookup request");

                    fop->error = EIO;

                    return EC_STATE_REPORT;
                }
            }
            else
            {
                uint64_t size;

                if (dict_get_uint64(fop->xdata, GF_CONTENT_KEY, &size) == 0)
                {
                    fop->size = size;
                    size = ec_adjust_size(fop->xl->private, size, 1);
                    if (dict_set_uint64(fop->xdata, GF_CONTENT_KEY, size) != 0)
                    {
                        gf_log("ec", GF_LOG_DEBUG, "Unable to update lookup "
                                                   "content size");
                    }
                }
            }
            if ((dict_set_uint64(fop->xdata, EC_XATTR_SIZE, 0) != 0) ||
                (dict_set_uint64(fop->xdata, EC_XATTR_VERSION, 0) != 0))
            {
                gf_log(fop->xl->name, GF_LOG_ERROR, "Unable to prepare lookup "
                                                    "request");

                fop->error = EIO;

                return EC_STATE_REPORT;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                    cbk->count);

                    ec_lookup_rebuild(fop->xl->private, fop, cbk);
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(LOOKUP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_LOOKUP, 0, target, minimum,
                               ec_wind_lookup, ec_manager_lookup, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL);
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
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
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

int32_t ec_manager_statfs(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
                else
                {
                    ec_t * ec = fop->xl->private;

                    cbk->statvfs.f_blocks *= ec->fragments;
                    cbk->statvfs.f_bfree *= ec->fragments;
                    cbk->statvfs.f_bavail *= ec->fragments;
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(STATFS) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_STATFS, 0, target, minimum,
                               ec_wind_statfs, ec_manager_statfs, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: xattrop */

int32_t ec_combine_xattrop(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                           ec_cbk_data_t * src)
{
    if (!ec_dict_compare(dst->dict, src->dict))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching dictionary in "
                                             "answers of 'GF_FOP_XATTROP'");

        return 0;
    }

    return 1;
}

int32_t ec_xattrop_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno, dict_t * xattr,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_XATTROP, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (xattr != NULL)
            {
                cbk->dict = dict_ref(xattr);
                if (cbk->dict == NULL)
                {
                    gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                     "dictionary.");

                    goto out;
                }
            }
        }
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
        }

        ec_combine(cbk, ec_combine_xattrop);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

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
            if (fop->fd == NULL)
            {
                ec_lock_prepare_inode(fop, &fop->loc[0], 1);
            }
            else
            {
                ec_lock_prepare_fd(fop, fop->fd, 1);
            }
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if (cbk != NULL)
            {
                if (!ec_dict_combine(cbk, EC_COMBINE_XDATA) ||
                    ((cbk->op_ret >= 0) && !ec_dict_combine(cbk,
                                                            EC_COMBINE_DICT)))
                {
                    if (cbk->op_ret >= 0)
                    {
                        cbk->op_ret = -1;
                        cbk->op_errno = EIO;
                    }
                }
                if (cbk->op_ret < 0)
                {
                    ec_fop_set_error(fop, cbk->op_errno);
                }
            }
            else
            {
                ec_fop_set_error(fop, EIO);
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(XATTROP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_XATTROP,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_xattrop, ec_manager_xattrop, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->xattrop_flags = optype;

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (xattr != NULL)
    {
        fop->dict = dict_ref(xattr);
        if (fop->dict == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: fxattrop */

int32_t ec_fxattrop_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno, dict_t * xattr,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FXATTROP, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (xattr != NULL)
            {
                cbk->dict = dict_ref(xattr);
                if (cbk->dict == NULL)
                {
                    gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                     "dictionary.");

                    goto out;
                }
            }
        }
        if (xdata != NULL)
        {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL)
            {
                gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
        }

        ec_combine(cbk, ec_combine_xattrop);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fxattrop(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fxattrop_cbk, (void *)(uintptr_t)idx,
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FXATTROP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FXATTROP,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fxattrop, ec_manager_xattrop, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->xattrop_flags = optype;

    if (fd != NULL)
    {
        fop->fd = fd_ref(fd);
        if (fop->fd == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "file descriptor.");

            goto out;
        }
    }
    if (xattr != NULL)
    {
        fop->dict = dict_ref(xattr);
        if (fop->dict == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }
    if (xdata != NULL)
    {
        fop->xdata = dict_ref(xdata);
        if (fop->xdata == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "dictionary.");

            goto out;
        }
    }

    error = 0;

out:
    if (fop != NULL)
    {
        ec_manager(fop, error);
    }
    else
    {
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}
