
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

/* FOP: access */

int32_t ec_access_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    if (!ec_dispatch_one_retry(fop, idx, op_ret, op_errno))
    {
        if (fop->cbks.access != NULL)
        {
            fop->cbks.access(fop->req_frame, fop, this, op_ret, op_errno,
                             xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_access(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_access_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->access,
                      &fop->loc[0], fop->int32, fop->xdata);
}

int32_t ec_manager_access(ec_fop_data_t * fop, int32_t state)
{
    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_DISPATCH:
            ec_dispatch_one(fop);

            return EC_STATE_REPORT;

        case -EC_STATE_REPORT:
            if (fop->cbks.access != NULL)
            {
                fop->cbks.access(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL);
            }

        case EC_STATE_REPORT:
            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_access(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_access_cbk_t func, void * data,
               loc_t * loc, int32_t mask, dict_t * xdata)
{
    ec_cbk_t callback = { .access = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(ACCESS) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_ACCESS, 0, target, minimum,
                               ec_wind_access, ec_manager_access, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = mask;

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
        func(frame, NULL, this, -1, EIO, NULL);
    }
}

/* FOP: getxattr */

int32_t ec_combine_getxattr(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                            ec_cbk_data_t * src)
{
    if (!ec_dict_compare(dst->dict, src->dict))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching dictionary in "
                                             "answers of 'GF_FOP_GETXATTR'");

        return 0;
    }

    return 1;
}

int32_t ec_getxattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno, dict_t * dict,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_GETXATTR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (dict != NULL)
            {
                cbk->dict = dict_ref(dict);
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

        ec_combine(cbk, ec_combine_getxattr);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_getxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_getxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->getxattr,
                      &fop->loc[0], fop->str[0], fop->xdata);
}

int32_t ec_manager_getxattr(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            if (fop->fd == NULL)
            {
                ec_lock_prepare_inode(fop, &fop->loc[0], 0);
            }
            else
            {
                ec_lock_prepare_fd(fop, fop->fd, 0);
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
                else
                {
                    if (cbk->xdata != NULL)
                    {
                        dict_del(cbk->xdata, EC_XATTR_SIZE);
                        dict_del(cbk->xdata, EC_XATTR_VERSION);
                    }
                    if (cbk->dict != NULL)
                    {
                        dict_del(cbk->dict, EC_XATTR_SIZE);
                        dict_del(cbk->dict, EC_XATTR_VERSION);
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

            if (fop->cbks.getxattr != NULL)
            {
                fop->cbks.getxattr(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                   cbk->op_errno, cbk->dict, cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.getxattr != NULL)
            {
                fop->cbks.getxattr(fop->req_frame, fop, fop->xl, -1,
                                   fop->error, NULL, NULL);
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

void ec_getxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_getxattr_cbk_t func, void * data,
                 loc_t * loc, const char * name, dict_t * xdata)
{
    ec_cbk_t callback = { .getxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(GETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_GETXATTR,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_getxattr, ec_manager_getxattr, callback,
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: fgetxattr */

int32_t ec_fgetxattr_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                         int32_t op_ret, int32_t op_errno, dict_t * dict,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FGETXATTR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (dict != NULL)
            {
                cbk->dict = dict_ref(dict);
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

        ec_combine(cbk, ec_combine_getxattr);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fgetxattr(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fgetxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fgetxattr,
                      fop->fd, fop->str[0], fop->xdata);
}

void ec_fgetxattr(call_frame_t * frame, xlator_t * this, uintptr_t target,
                  int32_t minimum, fop_fgetxattr_cbk_t func, void * data,
                  fd_t * fd, const char * name, dict_t * xdata)
{
    ec_cbk_t callback = { .fgetxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FGETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FGETXATTR,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fgetxattr, ec_manager_getxattr,
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: open */

int32_t ec_combine_open(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                        ec_cbk_data_t * src)
{
    if (dst->fd != src->fd)
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching fd in answers "
                                             "of 'GF_FOP_OPEN': %p <-> %p",
               dst->fd, src->fd);

        return 0;
    }

    return 1;
}

int32_t ec_open_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                    int32_t op_ret, int32_t op_errno, fd_t * fd,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_OPEN, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (fd != NULL)
            {
                cbk->fd = fd_ref(fd);
                if (cbk->fd == NULL)
                {
                    gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                     "file descriptor.");

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

        ec_combine(cbk, ec_combine_open);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_open(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_open_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->open,
                      &fop->loc[0], fop->int32, fop->fd, fop->xdata);
}

int32_t ec_manager_open(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;
    ec_fd_t * ctx;

    switch (state)
    {
        case EC_STATE_INIT:
            LOCK(&fop->fd->lock);

            ctx = __ec_fd_get(fop->fd, fop->xl);
            if ((ctx == NULL) || !ec_loc_from_loc(fop->xl, &ctx->loc,
                &fop->loc[0]))
            {
                UNLOCK(&fop->fd->lock);

                fop->error = EIO;

                return EC_STATE_REPORT;
            }

            if (ctx->flags == 0)
            {
                ctx->flags = fop->int32;
            }

            UNLOCK(&fop->fd->lock);

            if ((fop->int32 & O_ACCMODE) == O_WRONLY)
            {
                fop->int32 &= ~O_ACCMODE;
                fop->int32 |= O_RDWR;
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
                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->fd->inode,
                                   NULL);

                    LOCK(&fop->fd->lock);

                    ctx = __ec_fd_get(fop->fd, fop->xl);
                    if (ctx != NULL)
                    {
                        ctx->open |= cbk->mask;
                    }

                    UNLOCK(&fop->fd->lock);
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

            if (fop->cbks.open != NULL)
            {
                fop->cbks.open(fop->req_frame, fop, fop->xl, cbk->op_ret,
                               cbk->op_errno, cbk->fd, cbk->xdata);
            }

            return EC_STATE_END;

        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.open != NULL)
            {
                fop->cbks.open(fop->req_frame, fop, fop->xl, -1, fop->error,
                               NULL, NULL);
            }

            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_open(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_open_cbk_t func, void * data, loc_t * loc,
             int32_t flags, fd_t * fd, dict_t * xdata)
{
    ec_cbk_t callback = { .open = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(OPEN) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_OPEN, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_open, ec_manager_open,
                               callback, data);
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: readlink */

int32_t ec_combine_readlink(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                            ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 1))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_READLINK'");

        return 0;
    }

    return 1;
}

int32_t ec_readlink_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno, const char * path,
                        struct iatt * buf, dict_t * xdata)
{
    ec_fop_data_t * fop = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    if (op_ret > 0)
    {
        ec_iatt_rebuild(fop->xl->private, buf, 1, 1);
    }

    if (!ec_dispatch_one_retry(fop, idx, op_ret, op_errno))
    {
        if (fop->cbks.readlink != NULL)
        {
            fop->cbks.readlink(fop->req_frame, fop, this, op_ret, op_errno,
                               path, buf, xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_readlink(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_readlink_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readlink,
                      &fop->loc[0], fop->size, fop->xdata);
}

int32_t ec_manager_readlink(ec_fop_data_t * fop, int32_t state)
{
    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_DISPATCH:
            ec_dispatch_one(fop);

            return EC_STATE_REPORT;

        case -EC_STATE_REPORT:
            if (fop->cbks.readlink != NULL)
            {
                fop->cbks.readlink(fop->req_frame, fop, fop->xl, -1,
                                   fop->error, NULL, NULL, NULL);
            }

        case EC_STATE_REPORT:
            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_readlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_readlink_cbk_t func, void * data,
                 loc_t * loc, size_t size, dict_t * xdata)
{
    ec_cbk_t callback = { .readlink = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(READLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READLINK, 0, target,
                               minimum, ec_wind_readlink, ec_manager_readlink,
                               callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->size = size;

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

/* FOP: readv */

int32_t ec_readv_rebuild(ec_t * ec, ec_fop_data_t * fop, ec_cbk_data_t * cbk)
{
    ec_cbk_data_t * ans = NULL;
    struct iobref * iobref = NULL;
    struct iobuf * iobuf = NULL;
    uint8_t * buff = NULL, * ptr;
    size_t fsize = 0, size = 0, max = 0;
    int32_t i = 0;

    if (cbk->op_ret < 0)
    {
        goto out;
    }

    cbk->iatt[0].ia_size = fop->pre_size;

    if (cbk->op_ret > 0)
    {
        struct iovec vector[1];
        uint8_t * blocks[cbk->count];
        uint32_t values[cbk->count];

        fsize = cbk->op_ret;
        size = fsize * ec->fragments;
        buff = GF_MALLOC(size, gf_common_mt_char);
        if (buff == NULL)
        {
            goto out;
        }
        ptr = buff;
        for (i = 0, ans = cbk; ans != NULL; i++, ans = ans->next)
        {
            values[i] = ans->idx;
            blocks[i] = ptr;
            ptr += ec_iov_copy_to(ptr, ans->vector, ans->int32, 0, fsize);
        }

        iobref = iobref_new();
        if (iobref == NULL)
        {
            goto out;
        }
        iobuf = iobuf_get2(fop->xl->ctx->iobuf_pool, size);
        if (iobuf == NULL)
        {
            goto out;
        }
        if (iobref_add(iobref, iobuf) != 0)
        {
            goto out;
        }

        vector[0].iov_base = iobuf->ptr;
        vector[0].iov_len = ec_method_decode(fsize, ec->fragments, values,
                                             blocks, iobuf->ptr);

        iobuf_unref(iobuf);

        GF_FREE(buff);
        buff = NULL;

        vector[0].iov_base += fop->head;
        vector[0].iov_len -= fop->head;

        max = fop->offset * ec->fragments + size;
        if (max > cbk->iatt[0].ia_size)
        {
            max = cbk->iatt[0].ia_size;
        }
        max -= fop->offset * ec->fragments + fop->head;
        if (max > fop->user_size)
        {
            max = fop->user_size;
        }
        size -= fop->head;
        if (size > max)
        {
            vector[0].iov_len -= size - max;
            size = max;
        }

        cbk->op_ret = size;
        cbk->int32 = 1;

        iobref_unref(cbk->buffers);
        cbk->buffers = iobref;

        GF_FREE(cbk->vector);
        cbk->vector = iov_dup(vector, 1);
        if (cbk->vector == NULL)
        {
            cbk->op_ret = -1;
            cbk->op_errno = EIO;

            return 0;
        }
    }

    return 1;

out:
    if (iobuf != NULL)
    {
        iobuf_unref(iobuf);
    }
    if (iobref != NULL)
    {
        iobref_unref(iobref);
    }
    GF_FREE(buff);

    return 0;
}

int32_t ec_combine_readv(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_vector_compare(dst->vector, dst->int32, src->vector, src->int32))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching vector in "
                                             "answers of 'GF_FOP_READ'");

        return 0;
    }

    if (!ec_iatt_combine(dst->iatt, src->iatt, 1))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_READ'");

        return 0;
    }

    return 1;
}

int32_t ec_readv_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iovec * vector,
                     int32_t count, struct iatt * stbuf,
                     struct iobref * iobref, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_READ, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            cbk->int32 = count;

            if (count > 0)
            {
                cbk->vector = iov_dup(vector, count);
                if (cbk->vector == NULL)
                {
                    gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a "
                                                     "vector list.");

                    goto out;
                }
                cbk->int32 = count;
            }
            if (stbuf != NULL)
            {
                cbk->iatt[0] = *stbuf;
            }
            if (iobref != NULL)
            {
                cbk->buffers = iobref_ref(iobref);
                if (cbk->buffers == NULL)
                {
                    gf_log(this->name, GF_LOG_ERROR, "Failed to reference a "
                                                     "buffer.");

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

        if ((op_ret > 0) && ((op_ret % ec->fragment_size) != 0))
        {
            cbk->op_ret = -1;
            cbk->op_errno = EIO;
        }

        ec_combine(cbk, ec_combine_readv);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_readv(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_readv_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readv, fop->fd,
                      fop->size, fop->offset, fop->uint32, fop->xdata);
}

int32_t ec_manager_readv(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            fop->user_size = fop->size;
            fop->head = ec_adjust_offset(fop->xl->private, &fop->offset, 1);
            fop->size = ec_adjust_size(fop->xl->private, fop->size + fop->head,
                                       1);

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_fd(fop, fop->fd, 0);
            ec_lock(fop);

            return EC_STATE_GET_SIZE_AND_VERSION;

        case EC_STATE_GET_SIZE_AND_VERSION:
            ec_get_size_version(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_min(fop);

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
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 1,
                                    cbk->count);

                    if (!ec_readv_rebuild(fop->xl->private, fop, cbk))
                    {
                        ec_fop_set_error(fop, EIO);
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

            if (fop->cbks.readv != NULL)
            {
                fop->cbks.readv(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, cbk->vector, cbk->int32,
                                &cbk->iatt[0], cbk->buffers, cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.readv != NULL)
            {
                fop->cbks.readv(fop->req_frame, fop, fop->xl, -1, fop->error,
                                NULL, 0, NULL, NULL, NULL);
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

void ec_readv(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_readv_cbk_t func, void * data, fd_t * fd,
              size_t size, off_t offset, uint32_t flags, dict_t * xdata)
{
    ec_cbk_t callback = { .readv = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(READ) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READ, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_readv,
                               ec_manager_readv, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
    fop->offset = offset;
    fop->uint32 = flags;

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
        func(frame, NULL, this, -1, EIO, NULL, 0, NULL, NULL, NULL);
    }
}

/* FOP: stat */

int32_t ec_combine_stat(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                        ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 1))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_STAT'");

        return 0;
    }

    return 1;
}

int32_t ec_stat_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                    int32_t op_ret, int32_t op_errno, struct iatt * buf,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_STAT, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (buf != NULL)
            {
                cbk->iatt[0] = *buf;
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

        ec_combine(cbk, ec_combine_stat);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_stat(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_stat_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->stat,
                      &fop->loc[0], fop->xdata);
}

int32_t ec_manager_stat(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            if (fop->fd == NULL)
            {
                ec_lock_prepare_inode(fop, &fop->loc[0], 0);
            }
            else
            {
                ec_lock_prepare_fd(fop, fop->fd, 0);
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
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 1,
                                    cbk->count);

                    cbk->iatt[0].ia_size = fop->pre_size;
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

            if (fop->id == GF_FOP_STAT)
            {
                if (fop->cbks.stat != NULL)
                {
                    fop->cbks.stat(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                   cbk->op_errno, &cbk->iatt[0], cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fstat != NULL)
                {
                    fop->cbks.fstat(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                    cbk->op_errno, &cbk->iatt[0], cbk->xdata);
                }
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_STAT)
            {
                if (fop->cbks.stat != NULL)
                {
                    fop->cbks.stat(fop->req_frame, fop, fop->xl, -1,
                                   fop->error, NULL, NULL);
                }
            }
            else
            {
                if (fop->cbks.fstat != NULL)
                {
                    fop->cbks.fstat(fop->req_frame, fop, fop->xl, -1,
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

void ec_stat(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_stat_cbk_t func, void * data, loc_t * loc,
             dict_t * xdata)
{
    ec_cbk_t callback = { .stat = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(STAT) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_STAT,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_stat, ec_manager_stat, callback, data);
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

/* FOP: fstat */

int32_t ec_fstat_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iatt * buf,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FSTAT, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (buf != NULL)
            {
                cbk->iatt[0] = *buf;
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

        ec_combine(cbk, ec_combine_stat);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fstat(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fstat_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fstat, fop->fd,
                      fop->xdata);
}

void ec_fstat(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_fstat_cbk_t func, void * data, fd_t * fd,
              dict_t * xdata)
{
    ec_cbk_t callback = { .fstat = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FSTAT) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSTAT,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fstat, ec_manager_stat, callback, data);
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}
