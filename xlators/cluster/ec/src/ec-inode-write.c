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

/* FOP: removexattr */

int32_t ec_removexattr_cbk(call_frame_t * frame, void * cookie,
                           xlator_t * this, int32_t op_ret, int32_t op_errno,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_REMOVEXATTR, idx,
                               op_ret, op_errno);
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

void ec_wind_removexattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_removexattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->removexattr,
                      &fop->loc[0], fop->str[0], fop->xdata);
}

int32_t ec_manager_removexattr(ec_fop_data_t * fop, int32_t state)
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

            if (fop->id == GF_FOP_REMOVEXATTR)
            {
                if (fop->cbks.removexattr != NULL)
                {
                    fop->cbks.removexattr(fop->req_frame, fop, fop->xl,
                                          cbk->op_ret, cbk->op_errno,
                                          cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fremovexattr != NULL)
                {
                    fop->cbks.fremovexattr(fop->req_frame, fop, fop->xl,
                                           cbk->op_ret, cbk->op_errno,
                                           cbk->xdata);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_REMOVEXATTR)
            {
                if (fop->cbks.removexattr != NULL)
                {
                    fop->cbks.removexattr(fop->req_frame, fop, fop->xl, -1,
                                          fop->error, NULL);
                }
            }
            else
            {
                if (fop->cbks.fremovexattr != NULL)
                {
                    fop->cbks.fremovexattr(fop->req_frame, fop, fop->xl, -1,
                                           fop->error, NULL);
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

void ec_removexattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                    int32_t minimum, fop_removexattr_cbk_t func, void * data,
                    loc_t * loc, const char * name, dict_t * xdata)
{
    ec_cbk_t callback = { .removexattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(REMOVEXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_REMOVEXATTR,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_removexattr, ec_manager_removexattr,
                               callback, data);
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
    if (name != NULL)
    {
        fop->str[0] = gf_strdup(name);
        if (fop->str[0] == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a string.");

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

/* FOP: fremovexattr */

int32_t ec_fremovexattr_cbk(call_frame_t * frame, void * cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FREMOVEXATTR, idx,
                               op_ret, op_errno);
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

void ec_wind_fremovexattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fremovexattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fremovexattr,
                      fop->fd, fop->str[0], fop->xdata);
}

void ec_fremovexattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                     int32_t minimum, fop_fremovexattr_cbk_t func, void * data,
                     fd_t * fd, const char * name, dict_t * xdata)
{
    ec_cbk_t callback = { .fremovexattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FREMOVEXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FREMOVEXATTR,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fremovexattr, ec_manager_removexattr,
                               callback, data);
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
    if (name != NULL)
    {
        fop->str[0] = gf_strdup(name);
        if (fop->str[0] == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a string.");

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

/* FOP: setattr */

int32_t ec_combine_setattr(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                           ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_SETATTR'");

        return 0;
    }

    return 1;
}

int32_t ec_setattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt * preop_stbuf, struct iatt * postop_stbuf,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_SETATTR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (preop_stbuf != NULL)
            {
                cbk->iatt[0] = *preop_stbuf;
            }
            if (postop_stbuf != NULL)
            {
                cbk->iatt[1] = *postop_stbuf;
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

        ec_combine(cbk, ec_combine_setattr);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
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
            if (fop->fd == NULL)
            {
                ec_lock_prepare_inode(fop, &fop->loc[0], 1);
            }
            else
            {
                ec_lock_prepare_fd(fop, fop->fd, 1);
            }
            ec_lock(fop);

            return EC_STATE_GET_SIZE_AND_VERSION;

        case EC_STATE_GET_SIZE_AND_VERSION:
            ec_get_size_version(fop);

            return EC_STATE_DISPATCH;

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

                    cbk->iatt[0].ia_size = fop->pre_size;
                    cbk->iatt[1].ia_size = fop->pre_size;
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

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(SETATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SETATTR,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_setattr, ec_manager_setattr, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = valid;

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (stbuf != NULL)
    {
        fop->iatt = *stbuf;
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

/* FOP: fsetattr */

int32_t ec_fsetattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt * preop_stbuf, struct iatt * postop_stbuf,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSETATTR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (preop_stbuf != NULL)
            {
                cbk->iatt[0] = *preop_stbuf;
            }
            if (postop_stbuf != NULL)
            {
                cbk->iatt[1] = *postop_stbuf;
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

        ec_combine(cbk, ec_combine_setattr);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FSETATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSETATTR,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fsetattr, ec_manager_setattr, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = valid;

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
    if (stbuf != NULL)
    {
        fop->iatt = *stbuf;
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

/* FOP: setxattr */

int32_t ec_setxattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_SETXATTR, idx, op_ret,
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

void ec_wind_setxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_setxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->setxattr,
                      &fop->loc[0], fop->dict, fop->int32, fop->xdata);
}

int32_t ec_manager_setxattr(ec_fop_data_t * fop, int32_t state)
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

            if (fop->id == GF_FOP_SETXATTR)
            {
                if (fop->cbks.setxattr != NULL)
                {
                    fop->cbks.setxattr(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno, cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fsetxattr != NULL)
                {
                    fop->cbks.fsetxattr(fop->req_frame, fop, fop->xl,
                                        cbk->op_ret, cbk->op_errno,
                                        cbk->xdata);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_SETXATTR)
            {
                if (fop->cbks.setxattr != NULL)
                {
                    fop->cbks.setxattr(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL);
                }
            }
            else
            {
                if (fop->cbks.fsetxattr != NULL)
                {
                    fop->cbks.fsetxattr(fop->req_frame, fop, fop->xl, -1,
                                        fop->error, NULL);
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

void ec_setxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_setxattr_cbk_t func, void * data,
                 loc_t * loc, dict_t * dict, int32_t flags, dict_t * xdata)
{
    ec_cbk_t callback = { .setxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(SETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SETXATTR,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_setxattr, ec_manager_setxattr, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = flags;

    if (loc != NULL)
    {
        if (loc_copy(&fop->loc[0], loc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (dict != NULL)
    {
        fop->dict = dict_ref(dict);
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
        func(frame, NULL, this, -1, EIO, NULL);
    }
}

/* FOP: fsetxattr */

int32_t ec_fsetxattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSETXATTR, idx, op_ret,
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

void ec_wind_fsetxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fsetxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fsetxattr,
                      fop->fd, fop->dict, fop->int32, fop->xdata);
}

void ec_fsetxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_fsetxattr_cbk_t func, void * data,
                  fd_t * fd, dict_t * dict, int32_t flags, dict_t * xdata)
{
    ec_cbk_t callback = { .fsetxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FSETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSETXATTR,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fsetxattr, ec_manager_setxattr,
                               callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = flags;

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
    if (dict != NULL)
    {
        fop->dict = dict_ref(dict);
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
        func(frame, NULL, this, -1, EIO, NULL);
    }
}

/* FOP: truncate */

int32_t ec_truncate_write(ec_fop_data_t * fop, uintptr_t mask)
{
    ec_t * ec = fop->xl->private;
    struct iobref * iobref = NULL;
    struct iobuf * iobuf = NULL;
    struct iovec vector;
    int32_t ret = 0;

    iobref = iobref_new();
    if (iobref == NULL)
    {
        goto out;
    }
    iobuf = iobuf_get(fop->xl->ctx->iobuf_pool);
    if (iobuf == NULL)
    {
        goto out;
    }
    if (iobref_add(iobref, iobuf) != 0)
    {
        goto out;
    }

    vector.iov_base = iobuf->ptr;
    vector.iov_len = fop->offset * ec->fragments - fop->user_size;

    memset(iobuf->ptr, 0, vector.iov_len);

    iobuf = NULL;

    ec_writev(fop->frame, fop->xl, mask, fop->minimum, NULL, NULL, fop->fd,
              &vector, 1, fop->user_size, 0, iobref, NULL);

    ret = 1;

out:
    if (iobuf != NULL)
    {
        iobuf_unref(iobuf);
    }
    if (iobref != NULL)
    {
        iobref_unref(iobref);
    }

    return ret;
}

int32_t ec_truncate_open_cbk(call_frame_t * frame, void * cookie,
                             xlator_t * this, int32_t op_ret, int32_t op_errno,
                             fd_t * fd, dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;

    if (op_ret >= 0)
    {
        if (!ec_truncate_write(fop->parent, fop->answer->mask))
        {
            fop->error = EIO;
        }
    }

    return 0;
}

int32_t ec_truncate_clean(ec_fop_data_t * fop)
{
    ec_fd_t * ctx;

    if (fop->fd == NULL)
    {
        fop->fd = fd_create(fop->loc[0].inode, fop->frame->root->pid);
        if (fop->fd == NULL)
        {
            return 0;
        }
        ctx = ec_fd_get(fop->fd, fop->xl);
        if ((ctx == NULL) || (loc_copy(&ctx->loc, &fop->loc[0]) != 0))
        {
            return 0;
        }

        ctx->flags = O_RDWR;

        ec_open(fop->frame, fop->xl, fop->answer->mask, fop->minimum,
                ec_truncate_open_cbk, fop, &fop->loc[0], O_RDWR, fop->fd,
                NULL);

        return 1;
    }
    else
    {
        return ec_truncate_write(fop, fop->answer->mask);
    }
}

int32_t ec_combine_truncate(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                            ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_TRUNCATE'");

        return 0;
    }

    return 1;
}

int32_t ec_truncate_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_TRUNCATE, idx, op_ret,
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

        ec_combine(cbk, ec_combine_truncate);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
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
            fop->offset = ec_adjust_size(fop->xl->private, fop->offset, 1);

        /* Fall through */

        case EC_STATE_LOCK:
            if (fop->id == GF_FOP_TRUNCATE)
            {
                ec_lock_prepare_inode(fop, &fop->loc[0], 1);
            }
            else
            {
                ec_lock_prepare_fd(fop, fop->fd, 1);
            }
            ec_lock(fop);

            return EC_STATE_GET_SIZE_AND_VERSION;

        case EC_STATE_GET_SIZE_AND_VERSION:
            ec_get_size_version(fop);

            return EC_STATE_DISPATCH;

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

                    cbk->iatt[0].ia_size = fop->pre_size;
                    cbk->iatt[1].ia_size = fop->user_size;
                    fop->post_size = fop->user_size;
                    if ((fop->pre_size > fop->post_size) &&
                        (fop->user_size != fop->offset))
                    {
                        if (!ec_truncate_clean(fop))
                        {
                            ec_fop_set_error(fop, EIO);
                        }
                    }
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

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(TRUNCATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_TRUNCATE,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_truncate, ec_manager_truncate, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->offset = offset;

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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL);
    }
}

/* FOP: ftruncate */

int32_t ec_ftruncate_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno,
                         struct iatt * prebuf, struct iatt * postbuf,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FTRUNCATE, idx, op_ret,
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

        ec_combine(cbk, ec_combine_truncate);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FTRUNCATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FTRUNCATE,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_ftruncate, ec_manager_truncate,
                               callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->offset = offset;

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

/* FOP: writev */

int32_t ec_writev_init(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    struct iobref * iobref = NULL;
    struct iobuf * iobuf = NULL;
    void * ptr = NULL;
    ec_fd_t * ctx;

    ctx = ec_fd_get(fop->fd, fop->xl);
    if (ctx != NULL)
    {
        if ((ctx->flags & O_ACCMODE) == O_RDONLY)
        {
            return EBADF;
        }
    }

    fop->user_size = iov_length(fop->vector, fop->int32);
    fop->head = ec_adjust_offset(ec, &fop->offset, 0);
    fop->size = ec_adjust_size(ec, fop->user_size + fop->head, 0);

    iobref = iobref_new();
    if (iobref == NULL)
    {
        goto out;
    }
    iobuf = iobuf_get2(fop->xl->ctx->iobuf_pool, fop->size);
    if (iobuf == NULL)
    {
        goto out;
    }
    if (iobref_add(iobref, iobuf) != 0)
    {
        goto out;
    }

    ptr = iobuf->ptr + fop->head;
    ec_iov_copy_to(ptr, fop->vector, fop->int32, 0, fop->user_size);

    fop->vector[0].iov_base = iobuf->ptr;
    fop->vector[0].iov_len = fop->size;

    iobuf_unref(iobuf);

    iobref_unref(fop->buffers);
    fop->buffers = iobref;

    return 0;

out:
    if (iobuf != NULL)
    {
        iobuf_unref(iobuf);
    }
    if (iobref != NULL)
    {
        iobref_unref(iobref);
    }

    return EIO;
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

void ec_writev_start(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    size_t tail;

    if (fop->head > 0)
    {
        ec_readv(fop->frame, fop->xl, -1, EC_MINIMUM_MIN, ec_writev_merge_head,
                 NULL, fop->fd, ec->stripe_size, fop->offset, 0, NULL);
    }
    tail = fop->size - fop->user_size - fop->head;
    if ((tail > 0) && ((fop->head == 0) || (fop->size > ec->stripe_size)))
    {
        if (fop->pre_size > fop->offset + fop->head + fop->user_size)
        {
            ec_readv(fop->frame, fop->xl, -1, EC_MINIMUM_MIN,
                     ec_writev_merge_tail, NULL, fop->fd, ec->stripe_size,
                     fop->offset + fop->size - ec->stripe_size, 0, NULL);
        }
        else
        {
            memset(fop->vector[0].iov_base + fop->size - tail, 0, tail);
        }
    }
}

int32_t ec_combine_writev(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_WRITE'");

        return 0;
    }

    return 1;
}

int32_t ec_writev_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, struct iatt * prebuf,
                      struct iatt * postbuf, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    ec_cbk_data_t * cbk = NULL;
    ec_t * ec = this->private;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_WRITE, idx, op_ret,
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

        if ((op_ret > 0) && ((op_ret % ec->fragment_size) != 0))
        {
            cbk->op_ret = -1;
            cbk->op_errno = EIO;
        }

        ec_combine(cbk, ec_combine_writev);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_writev(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    struct iovec vector[1];
    struct iobref * iobref = NULL;
    struct iobuf * iobuf = NULL;
    ssize_t size = 0, bufsize = 0;

    iobref = iobref_new();
    if (iobref == NULL)
    {
        goto out;
    }

    size = fop->vector[0].iov_len;
    bufsize = size / ec->fragments;

    iobuf = iobuf_get2(fop->xl->ctx->iobuf_pool, bufsize);
    if (iobuf == NULL)
    {
        goto out;
    }
    if (iobref_add(iobref, iobuf) != 0)
    {
        goto out;
    }

    ec_method_encode(size, ec->fragments, idx, fop->vector[0].iov_base,
                     iobuf->ptr);

    vector[0].iov_base = iobuf->ptr;
    vector[0].iov_len = bufsize;

    iobuf_unref(iobuf);

    STACK_WIND_COOKIE(fop->frame, ec_writev_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->writev,
                      fop->fd, vector, 1, fop->offset / ec->fragments,
                      fop->uint32, iobref, fop->xdata);

    iobref_unref(iobref);

    return;

out:
    if (iobuf != NULL)
    {
        iobuf_unref(iobuf);
    }
    if (iobref != NULL)
    {
        iobref_unref(iobref);
    }

    ec_writev_cbk(fop->frame, (void *)(uintptr_t)idx, fop->xl, -1, EIO, NULL,
                  NULL, NULL);
}

int32_t ec_manager_writev(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            fop->error = ec_writev_init(fop);
            if (fop->error != 0)
            {
                return EC_STATE_REPORT;
            }

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_fd(fop, fop->fd, 1);
            ec_lock(fop);

            return EC_STATE_GET_SIZE_AND_VERSION;

        case EC_STATE_GET_SIZE_AND_VERSION:
            ec_get_size_version(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_writev_start(fop);

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
                    ec_t * ec = fop->xl->private;
                    size_t size;

                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 2,
                                    cbk->count);

                    size = fop->offset + fop->head + fop->user_size;
                    if (size > fop->pre_size)
                    {
                        fop->post_size = size;
                    }

                    cbk->iatt[0].ia_size = fop->pre_size;
                    cbk->iatt[1].ia_size = fop->post_size;

                    cbk->op_ret *= ec->fragments;
                    if (cbk->op_ret < fop->head)
                    {
                        cbk->op_ret = 0;
                    }
                    else
                    {
                        cbk->op_ret -= fop->head;
                    }
                    if (cbk->op_ret > fop->user_size)
                    {
                        cbk->op_ret = fop->user_size;
                    }
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

            if (fop->cbks.writev != NULL)
            {
                fop->cbks.writev(fop->req_frame, fop, fop->xl, cbk->op_ret,
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
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
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
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(WRITE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_WRITE,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_writev, ec_manager_writev, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = count;
    fop->offset = offset;
    fop->uint32 = flags;

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
    if (count > 0)
    {
        fop->vector = iov_dup(vector, count);
        if (fop->vector == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a "
                                             "vector list.");

            goto out;
        }
        fop->int32 = count;
    }
    if (iobref != NULL)
    {
        fop->buffers = iobref_ref(iobref);
        if (fop->buffers == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                             "buffer.");

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
