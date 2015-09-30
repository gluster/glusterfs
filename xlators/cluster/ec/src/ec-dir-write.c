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

/* FOP: create */

int32_t ec_combine_create(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (dst->fd != src->fd)
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching fd in answers "
                                             "of 'GF_FOP_CREATE': %p <-> %p",
               dst->fd, src->fd);

        return 0;
    }

    if (!ec_iatt_combine(dst->iatt, src->iatt, 3))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_CREATE'");

        return 0;
    }

    return 1;
}

int32_t ec_create_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, fd_t * fd,
                      inode_t * inode, struct iatt * buf,
                      struct iatt * preparent, struct iatt * postparent,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_CREATE, idx, op_ret,
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
            if (preparent != NULL)
            {
                cbk->iatt[1] = *preparent;
            }
            if (postparent != NULL)
            {
                cbk->iatt[2] = *postparent;
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

        ec_combine(cbk, ec_combine_create);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_create(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_create_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->create,
                      &fop->loc[0], fop->int32, fop->mode[0], fop->mode[1],
                      fop->fd, fop->xdata);
}

int32_t ec_manager_create(ec_fop_data_t * fop, int32_t state)
{
    ec_t * ec;
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

            if (fop->xdata == NULL)
            {
                fop->xdata = dict_new();
                if (fop->xdata == NULL)
                {
                    fop->error = EIO;

                    return EC_STATE_REPORT;
                }
            }

            ec = fop->xl->private;

            fop->config.version = EC_CONFIG_VERSION;
            fop->config.algorithm = EC_CONFIG_ALGORITHM;
            fop->config.gf_word_size = EC_GF_BITS;
            fop->config.bricks = ec->nodes;
            fop->config.redundancy = ec->redundancy;
            fop->config.chunk_size = EC_METHOD_CHUNK_SIZE;

            if (ec_dict_set_config(fop->xdata, EC_XATTR_CONFIG,
                                   &fop->config) < 0)
            {
                fop->error = EIO;

                return EC_STATE_REPORT;
            }

            fop->int32 &= ~O_ACCMODE;
            fop->int32 |= O_RDWR;

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3,
                                    cbk->count);

                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode,
                                   &cbk->iatt[0]);

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

            if (fop->cbks.create != NULL)
            {
                fop->cbks.create(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                 cbk->op_errno, cbk->fd, cbk->inode,
                                 &cbk->iatt[0], &cbk->iatt[1], &cbk->iatt[2],
                                 cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.create != NULL)
            {
                fop->cbks.create(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL, NULL, NULL, NULL, NULL, NULL);
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

void ec_create(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_create_cbk_t func, void * data,
               loc_t * loc, int32_t flags, mode_t mode, mode_t umask,
               fd_t * fd, dict_t * xdata)
{
    ec_cbk_t callback = { .create = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(CREATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_CREATE,
                               EC_FLAG_UPDATE_LOC_PARENT |
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_create, ec_manager_create, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = flags;
    fop->mode[0] = mode;
    fop->mode[1] = umask;

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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: link */

int32_t ec_combine_link(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                        ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 3))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_LINK'");

        return 0;
    }

    return 1;
}

int32_t ec_link_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                    int32_t op_ret, int32_t op_errno, inode_t * inode,
                    struct iatt * buf, struct iatt * preparent,
                    struct iatt * postparent, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_LINK, idx, op_ret,
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
            if (preparent != NULL)
            {
                cbk->iatt[1] = *preparent;
            }
            if (postparent != NULL)
            {
                cbk->iatt[2] = *postparent;
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

        ec_combine(cbk, ec_combine_link);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_link(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_link_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->link,
                      &fop->loc[0], &fop->loc[1], fop->xdata);
}

int32_t ec_manager_link(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            // Parent entry of fop->loc[0] should be locked, but I don't
            // receive enough information to do it (fop->loc[0].parent is
            // NULL).
            ec_lock_prepare_entry(fop, &fop->loc[1], 1);
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
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3,
                                    cbk->count);

                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode,
                                   &cbk->iatt[0]);

                    if (cbk->iatt[0].ia_type == IA_IFREG)
                    {
                        cbk->iatt[0].ia_size = fop->pre_size;
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

            if (fop->cbks.link != NULL)
            {
                fop->cbks.link(fop->req_frame, fop, fop->xl, cbk->op_ret,
                               cbk->op_errno, cbk->inode, &cbk->iatt[0],
                               &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.link != NULL)
            {
                fop->cbks.link(fop->req_frame, fop, fop->xl, -1, fop->error,
                               NULL, NULL, NULL, NULL, NULL);
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

void ec_link(call_frame_t * frame, xlator_t * this, uintptr_t target,
             int32_t minimum, fop_link_cbk_t func, void * data, loc_t * oldloc,
             loc_t * newloc, dict_t * xdata)
{
    ec_cbk_t callback = { .link = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(LINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_LINK, 0, target, minimum,
                               ec_wind_link, ec_manager_link, callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    if (oldloc != NULL)
    {
        if (loc_copy(&fop->loc[0], oldloc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (newloc != NULL)
    {
        if (loc_copy(&fop->loc[1], newloc) != 0)
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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: mkdir */

int32_t ec_combine_mkdir(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 3))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_MKDIR'");

        return 0;
    }

    return 1;
}

int32_t ec_mkdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, inode_t * inode,
                     struct iatt * buf, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_MKDIR, idx, op_ret,
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
            if (preparent != NULL)
            {
                cbk->iatt[1] = *preparent;
            }
            if (postparent != NULL)
            {
                cbk->iatt[2] = *postparent;
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

        ec_combine(cbk, ec_combine_mkdir);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_mkdir(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_mkdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->mkdir,
                      &fop->loc[0], fop->mode[0], fop->mode[1], fop->xdata);
}

int32_t ec_manager_mkdir(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3,
                                    cbk->count);

                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode,
                                   &cbk->iatt[0]);
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

            if (fop->cbks.mkdir != NULL)
            {
                fop->cbks.mkdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, cbk->inode, &cbk->iatt[0],
                                &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.mkdir != NULL)
            {
                fop->cbks.mkdir(fop->req_frame, fop, fop->xl, -1, fop->error,
                                NULL, NULL, NULL, NULL, NULL);
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

void ec_mkdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_mkdir_cbk_t func, void * data, loc_t * loc,
              mode_t mode, mode_t umask, dict_t * xdata)
{
    ec_cbk_t callback = { .mkdir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(MKDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_MKDIR,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_mkdir, ec_manager_mkdir, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->mode[0] = mode;
    fop->mode[1] = umask;

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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: mknod */

int32_t ec_combine_mknod(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 3))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_MKNOD'");

        return 0;
    }

    return 1;
}

int32_t ec_mknod_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, inode_t * inode,
                     struct iatt * buf, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_MKNOD, idx, op_ret,
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
            if (preparent != NULL)
            {
                cbk->iatt[1] = *preparent;
            }
            if (postparent != NULL)
            {
                cbk->iatt[2] = *postparent;
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

        ec_combine(cbk, ec_combine_mknod);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_mknod(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_mknod_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->mknod,
                      &fop->loc[0], fop->mode[0], fop->dev, fop->mode[1],
                      fop->xdata);
}

int32_t ec_manager_mknod(ec_fop_data_t * fop, int32_t state)
{
    ec_t *ec;
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            if (S_ISREG(fop->mode[0])) {
                if (fop->xdata == NULL) {
                    fop->xdata = dict_new();
                    if (fop->xdata == NULL) {
                        fop->error = EIO;

                        return EC_STATE_REPORT;
                    }
                }

                ec = fop->xl->private;

                fop->config.version = EC_CONFIG_VERSION;
                fop->config.algorithm = EC_CONFIG_ALGORITHM;
                fop->config.gf_word_size = EC_GF_BITS;
                fop->config.bricks = ec->nodes;
                fop->config.redundancy = ec->redundancy;
                fop->config.chunk_size = EC_METHOD_CHUNK_SIZE;

                if (ec_dict_set_config(fop->xdata, EC_XATTR_CONFIG,
                                       &fop->config) < 0) {
                    fop->error = EIO;

                    return EC_STATE_REPORT;
                }
            }

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3,
                                    cbk->count);

                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode,
                                   &cbk->iatt[0]);
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

            if (fop->cbks.mknod != NULL)
            {
                fop->cbks.mknod(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, cbk->inode, &cbk->iatt[0],
                                &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.mknod != NULL)
            {
                fop->cbks.mknod(fop->req_frame, fop, fop->xl, -1, fop->error,
                                NULL, NULL, NULL, NULL, NULL);
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

void ec_mknod(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_mknod_cbk_t func, void * data, loc_t * loc,
              mode_t mode, dev_t rdev, mode_t umask, dict_t * xdata)
{
    ec_cbk_t callback = { .mknod = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(MKNOD) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_MKNOD,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_mknod, ec_manager_mknod, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->mode[0] = mode;
    fop->dev = rdev;
    fop->mode[1] = umask;

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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: rename */

int32_t ec_combine_rename(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 5))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_RENAME'");

        return 0;
    }

    return 1;
}

int32_t ec_rename_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, struct iatt * buf,
                      struct iatt * preoldparent, struct iatt * postoldparent,
                      struct iatt * prenewparent, struct iatt * postnewparent,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_RENAME, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (buf != NULL)
            {
                cbk->iatt[0] = *buf;
            }
            if (preoldparent != NULL)
            {
                cbk->iatt[1] = *preoldparent;
            }
            if (postoldparent != NULL)
            {
                cbk->iatt[2] = *postoldparent;
            }
            if (prenewparent != NULL)
            {
                cbk->iatt[3] = *prenewparent;
            }
            if (postnewparent != NULL)
            {
                cbk->iatt[4] = *postnewparent;
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

        ec_combine(cbk, ec_combine_rename);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_rename(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_rename_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->rename,
                      &fop->loc[0], &fop->loc[1], fop->xdata);
}

int32_t ec_manager_rename(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
            ec_lock_prepare_entry(fop, &fop->loc[1], 1);
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
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 5,
                                    cbk->count);

                    if (cbk->iatt[0].ia_type == IA_IFREG)
                    {
                        cbk->iatt[0].ia_size = fop->pre_size;
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

            if (fop->cbks.rename != NULL)
            {
                fop->cbks.rename(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                 cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                 &cbk->iatt[2], &cbk->iatt[3], &cbk->iatt[4],
                                 cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_GET_SIZE_AND_VERSION:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.rename != NULL)
            {
                fop->cbks.rename(fop->req_frame, fop, fop->xl, -1, fop->error,
                                 NULL, NULL, NULL, NULL, NULL, NULL);
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

void ec_rename(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_rename_cbk_t func, void * data,
               loc_t * oldloc, loc_t * newloc, dict_t * xdata)
{
    ec_cbk_t callback = { .rename = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(RENAME) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_RENAME,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_rename, ec_manager_rename, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    if (oldloc != NULL)
    {
        if (loc_copy(&fop->loc[0], oldloc) != 0)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to copy a location.");

            goto out;
        }
    }
    if (newloc != NULL)
    {
        if (loc_copy(&fop->loc[1], newloc) != 0)
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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: rmdir */

int32_t ec_combine_rmdir(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_RMDIR'");

        return 0;
    }

    return 1;
}

int32_t ec_rmdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_RMDIR, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (preparent != NULL)
            {
                cbk->iatt[0] = *preparent;
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

        ec_combine(cbk, ec_combine_rmdir);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_rmdir(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_rmdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->rmdir,
                      &fop->loc[0], fop->int32, fop->xdata);
}

int32_t ec_manager_rmdir(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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

            if (fop->cbks.rmdir != NULL)
            {
                fop->cbks.rmdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, &cbk->iatt[0], &cbk->iatt[1],
                                cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.rmdir != NULL)
            {
                fop->cbks.rmdir(fop->req_frame, fop, fop->xl, -1, fop->error,
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

void ec_rmdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_rmdir_cbk_t func, void * data, loc_t * loc,
              int xflags, dict_t * xdata)
{
    ec_cbk_t callback = { .rmdir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(RMDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_RMDIR,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_rmdir, ec_manager_rmdir, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = xflags;

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

/* FOP: symlink */

int32_t ec_combine_symlink(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                           ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 3))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_SYMLINK'");

        return 0;
    }

    return 1;
}

int32_t ec_symlink_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno, inode_t * inode,
                       struct iatt * buf, struct iatt * preparent,
                       struct iatt * postparent, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_SYMLINK, idx, op_ret,
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
            if (preparent != NULL)
            {
                cbk->iatt[1] = *preparent;
            }
            if (postparent != NULL)
            {
                cbk->iatt[2] = *postparent;
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

        ec_combine(cbk, ec_combine_symlink);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_symlink(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_symlink_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->symlink,
                      fop->str[0], &fop->loc[0], fop->mode[0], fop->xdata);
}

int32_t ec_manager_symlink(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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
                else
                {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3,
                                    cbk->count);

                    ec_loc_prepare(fop->xl, &fop->loc[0], cbk->inode,
                                   &cbk->iatt[0]);
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

            if (fop->cbks.symlink != NULL)
            {
                fop->cbks.symlink(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                  cbk->op_errno, cbk->inode, &cbk->iatt[0],
                                  &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.symlink != NULL)
            {
                fop->cbks.symlink(fop->req_frame, fop, fop->xl, -1, fop->error,
                                  NULL, NULL, NULL, NULL, NULL);
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

void ec_symlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_symlink_cbk_t func, void * data,
                const char * linkname, loc_t * loc, mode_t umask,
                dict_t * xdata)
{
    ec_cbk_t callback = { .symlink = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(SYMLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SYMLINK,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_symlink, ec_manager_symlink, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->mode[0] = umask;

    if (linkname != NULL)
    {
        fop->str[0] = gf_strdup(linkname);
        if (fop->str[0] == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a string.");

            goto out;
        }
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
        func(frame, NULL, this, -1, EIO, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: unlink */

int32_t ec_combine_unlink(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                          ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(dst->iatt, src->iatt, 2))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching iatt in "
                                             "answers of 'GF_FOP_UNLINK'");

        return 0;
    }

    return 1;
}

int32_t ec_unlink_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt * preparent, struct iatt * postparent,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_UNLINK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        if (op_ret >= 0)
        {
            if (preparent != NULL)
            {
                cbk->iatt[0] = *preparent;
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

        ec_combine(cbk, ec_combine_unlink);
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_unlink(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_unlink_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->unlink,
                      &fop->loc[0], fop->int32, fop->xdata);
}

int32_t ec_manager_unlink(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_entry(fop, &fop->loc[0], 1);
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

            if (fop->cbks.unlink != NULL)
            {
                fop->cbks.unlink(fop->req_frame, fop, fop->xl, cbk->op_ret,
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

            if (fop->cbks.unlink != NULL)
            {
                fop->cbks.unlink(fop->req_frame, fop, fop->xl, -1, fop->error,
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

void ec_unlink(call_frame_t * frame, xlator_t * this, uintptr_t target,
               int32_t minimum, fop_unlink_cbk_t func, void * data,
               loc_t * loc, int xflags, dict_t * xdata)
{
    ec_cbk_t callback = { .unlink = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(UNLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_UNLINK,
                               EC_FLAG_UPDATE_LOC_PARENT, target, minimum,
                               ec_wind_unlink, ec_manager_unlink, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = xflags;

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
