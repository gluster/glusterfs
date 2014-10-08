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

/* FOP: opendir */

int32_t ec_combine_opendir(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                           ec_cbk_data_t * src)
{
    if (dst->fd != src->fd)
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching fd in answers "
                                             "of 'GF_FOP_OPENDIR': %p <-> %p",
               dst->fd, src->fd);

        return 0;
    }

    return 1;
}

int32_t ec_opendir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_OPENDIR, idx, op_ret,
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

        ec_combine(cbk, ec_combine_opendir);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_opendir(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_opendir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->opendir,
                      &fop->loc[0], fop->fd, fop->xdata);
}

int32_t ec_manager_opendir(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;
    ec_fd_t *ctx;

    switch (state)
    {
        case EC_STATE_INIT:
            LOCK(&fop->fd->lock);

            ctx = __ec_fd_get(fop->fd, fop->xl);
            if ((ctx == NULL) || !ec_loc_from_loc(fop->xl, &ctx->loc,
                &fop->loc[0])) {
                UNLOCK(&fop->fd->lock);

                fop->error = EIO;

                return EC_STATE_REPORT;
            }

            UNLOCK(&fop->fd->lock);

            /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 0);
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
            }
            else
            {
                ec_fop_set_error(fop, EIO);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.opendir != NULL)
            {
                fop->cbks.opendir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                  cbk->op_errno, cbk->fd, cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.opendir != NULL)
            {
                fop->cbks.opendir(fop->req_frame, fop, fop->xl, -1, fop->error,
                                  NULL, NULL);
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

void ec_opendir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_opendir_cbk_t func, void * data,
                loc_t * loc, fd_t * fd, dict_t * xdata)
{
    ec_cbk_t callback = { .opendir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(OPENDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_OPENDIR, EC_FLAG_UPDATE_FD,
                               target, minimum, ec_wind_opendir,
                               ec_manager_opendir, callback, data);
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

/* FOP: readdir */

void ec_adjust_readdir(ec_t * ec, int32_t idx, gf_dirent_t * entries)
{
    gf_dirent_t * entry;

    list_for_each_entry(entry, &entries->list, list)
    {
        entry->d_off = ec_itransform(ec, idx, entry->d_off);

        if (entry->d_stat.ia_type == IA_IFREG)
        {
            if ((entry->dict == NULL) ||
                (ec_dict_del_number(entry->dict, EC_XATTR_SIZE,
                                    &entry->d_stat.ia_size) != 0))
            {
                gf_log(ec->xl->name, GF_LOG_WARNING, "Unable to get exact "
                                                     "file size.");

                entry->d_stat.ia_size *= ec->fragments;
            }

            ec_iatt_rebuild(ec, &entry->d_stat, 1, 1);
        }
    }
}

int32_t ec_readdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno, gf_dirent_t * entries,
                       dict_t * xdata)
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
        ec_adjust_readdir(fop->xl->private, idx, entries);
    }

    if (!ec_dispatch_one_retry(fop, idx, op_ret, op_errno))
    {
        if (fop->cbks.readdir != NULL)
        {
            fop->cbks.readdir(fop->req_frame, fop, this, op_ret, op_errno,
                              entries, xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_readdir(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_readdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readdir,
                      fop->fd, fop->size, fop->offset, fop->xdata);
}

int32_t ec_manager_readdir(ec_fop_data_t * fop, int32_t state)
{
    switch (state)
    {
        case EC_STATE_INIT:
            if (fop->xdata == NULL)
            {
                fop->xdata = dict_new();
                if (fop->xdata == NULL)
                {
                    gf_log(fop->xl->name, GF_LOG_ERROR, "Unable to prepare "
                                                        "readdirp request");

                    fop->error = EIO;

                    return EC_STATE_REPORT;
                }
            }
            if (dict_set_uint64(fop->xdata, EC_XATTR_SIZE, 0) != 0)
            {
                gf_log(fop->xl->name, GF_LOG_ERROR, "Unable to prepare "
                                                    "readdirp request");

                fop->error = EIO;

                return EC_STATE_REPORT;
            }

            if (fop->offset != 0)
            {
                int32_t idx;

                fop->offset = ec_deitransform(fop->xl->private, &idx,
                                              fop->offset);
                fop->mask &= 1ULL << idx;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_one(fop);

            return EC_STATE_REPORT;

        case -EC_STATE_REPORT:
            if (fop->id == GF_FOP_READDIR)
            {
                if (fop->cbks.readdir != NULL)
                {
                    fop->cbks.readdir(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL, NULL);
                }
            }
            else
            {
                if (fop->cbks.readdirp != NULL)
                {
                    fop->cbks.readdirp(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL, NULL);
                }
            }

        case EC_STATE_REPORT:
            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_readdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_readdir_cbk_t func, void * data,
                fd_t * fd, size_t size, off_t offset, dict_t * xdata)
{
    ec_cbk_t callback = { .readdir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(READDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READDIR, 0, target, minimum,
                               ec_wind_readdir, ec_manager_readdir, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}

/* FOP: readdirp */

int32_t ec_readdirp_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                        int32_t op_ret, int32_t op_errno,
                        gf_dirent_t * entries, dict_t * xdata)
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
        ec_adjust_readdir(fop->xl->private, idx, entries);
    }

    if (!ec_dispatch_one_retry(fop, idx, op_ret, op_errno))
    {
        if (fop->cbks.readdirp != NULL)
        {
            fop->cbks.readdirp(fop->req_frame, fop, this, op_ret, op_errno,
                               entries, xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_readdirp(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_readdirp_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readdirp,
                      fop->fd, fop->size, fop->offset, fop->xdata);
}

void ec_readdirp(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_readdirp_cbk_t func, void * data,
                 fd_t * fd, size_t size, off_t offset, dict_t * xdata)
{
    ec_cbk_t callback = { .readdirp = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(READDIRP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READDIRP, 0, target,
                               minimum, ec_wind_readdirp, ec_manager_readdir,
                               callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
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
        func(frame, NULL, this, -1, EIO, NULL, NULL);
    }
}
