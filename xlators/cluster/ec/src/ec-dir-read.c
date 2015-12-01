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

/* FOP: opendir */

int32_t ec_combine_opendir(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                           ec_cbk_data_t * src)
{
    if (dst->fd != src->fd)
    {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_FD_MISMATCH, "Mismatching fd in answers "
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
                    gf_msg (this->name, GF_LOG_ERROR, 0,
                            EC_MSG_FILE_DESC_REF_FAIL, "Failed to reference a "
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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
    int32_t err;

    switch (state)
    {
        case EC_STATE_INIT:
            LOCK(&fop->fd->lock);

            ctx = __ec_fd_get(fop->fd, fop->xl);
            if (ctx == NULL) {
                UNLOCK(&fop->fd->lock);

                fop->error = ENOMEM;

                return EC_STATE_REPORT;
            }
            err = ec_loc_from_loc(fop->xl, &ctx->loc, &fop->loc[0]);
            if (err != 0) {
                UNLOCK(&fop->fd->lock);

                fop->error = -err;

                return EC_STATE_REPORT;
            }

            UNLOCK(&fop->fd->lock);

            /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_inode(fop, &fop->loc[0], EC_QUERY_INFO);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                /* Save which subvolumes successfully opened the directory.
                 * If ctx->open is 0, it means that readdir cannot be
                 * processed in this directory.
                 */
                LOCK(&fop->fd->lock);

                ctx = __ec_fd_get(fop->fd, fop->xl);
                if (ctx != NULL) {
                    ctx->open |= cbk->mask;
                }

                UNLOCK(&fop->fd->lock);
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

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(OPENDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_OPENDIR,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_opendir, ec_manager_opendir, callback,
                               data);
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
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}

/* Returns -1 if client_id is invalid else index of child subvol in xl_list */
int
ec_deitransform (xlator_t *this, off_t offset)
{
        int  idx       = -1;
        int  client_id = -1;
        ec_t *ec       = this->private;
        char id[32]    = {0};
        int err;

        client_id = gf_deitransform (this, offset);
        sprintf (id, "%d", client_id);
        err = dict_get_int32 (ec->leaf_to_subvolid, id, &idx);
        if (err < 0) {
                idx = err;
                goto out;
        }

out:
        if (idx < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        EC_MSG_INVALID_REQUEST,
                        "Invalid index %d in readdirp request", client_id);
                idx = -EINVAL;
        }
        return idx;
}

/* FOP: readdir */

void ec_adjust_readdirp (ec_t *ec, int32_t idx, gf_dirent_t *entries)
{
    gf_dirent_t * entry;

    list_for_each_entry(entry, &entries->list, list)
    {
        if (!entry->inode)
                continue;

        if (entry->d_stat.ia_type == IA_IFREG)
        {
            if ((entry->dict == NULL) ||
                (ec_dict_del_number(entry->dict, EC_XATTR_SIZE,
                                    &entry->d_stat.ia_size) != 0)) {
                    inode_unref (entry->inode);
                    entry->inode = NULL;
            } else {
                ec_iatt_rebuild(ec, &entry->d_stat, 1, 1);
            }
        }
    }
}

int32_t
ec_common_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       gf_dirent_t *entries, dict_t *xdata)
{
    ec_fop_data_t *fop = NULL;
    ec_cbk_data_t *cbk = NULL;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate (frame, this, fop, fop->id,
                                idx, op_ret, op_errno);
    if (cbk) {
        if (xdata)
                cbk->xdata = dict_ref (xdata);
        if (cbk->op_ret >= 0)
                list_splice_init (&entries->list,
                                  &cbk->entries.list);
        ec_combine (cbk, NULL);
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

    STACK_WIND_COOKIE(fop->frame, ec_common_readdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readdir,
                      fop->fd, fop->size, fop->offset, fop->xdata);
}

int32_t ec_manager_readdir(ec_fop_data_t * fop, int32_t state)
{
    ec_fd_t *ctx = NULL;
    ec_cbk_data_t *cbk = NULL;

    switch (state)
    {
        case EC_STATE_INIT:
            /* Return error if opendir has not been successfully called on
             * any subvolume. */
            ctx = ec_fd_get(fop->fd, fop->xl);
            if ((ctx == NULL) || (ctx->open == 0)) {
                fop->error = EINVAL;

                return EC_STATE_REPORT;
            }

            if (fop->id == GF_FOP_READDIRP) {
                    int32_t err;

                    if (fop->xdata == NULL) {
                        fop->xdata = dict_new();
                        if (fop->xdata == NULL) {
                            fop->error = ENOMEM;

                            return EC_STATE_REPORT;
                        }
                    }

                    err = dict_set_uint64(fop->xdata, EC_XATTR_SIZE, 0);
                    if (err != 0) {
                        fop->error = -err;

                        return EC_STATE_REPORT;
                    }
            }

            if (fop->offset != 0)
            {
            /* Non-zero offset is irrecoverable error as the offset may not be
             * valid on other bricks*/
                int32_t idx = -1;

                idx = ec_deitransform (fop->xl, fop->offset);

                if (idx < 0) {
                        fop->error = -idx;
                        return EC_STATE_REPORT;
                }
                fop->mask &= 1ULL << idx;
            } else {
                    ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
                    ec_lock(fop);
            }

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_one(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            if (ec_dispatch_one_retry(fop, &cbk)) {
                return EC_STATE_DISPATCH;
            }

            if ((cbk != NULL) && (cbk->op_ret > 0) &&
                (fop->id == GF_FOP_READDIRP)) {
                ec_adjust_readdirp (fop->xl->private, cbk->idx, &cbk->entries);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;
            GF_ASSERT (cbk);
            if (fop->id == GF_FOP_READDIR) {
                if (fop->cbks.readdir != NULL) {
                    fop->cbks.readdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                      cbk->op_errno, &cbk->entries, cbk->xdata);
                }
            } else {
                if (fop->cbks.readdirp != NULL) {
                    fop->cbks.readdirp(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno,
                                       &cbk->entries, cbk->xdata);
                }
            }
            if (fop->offset == 0)
                    return EC_STATE_LOCK_REUSE;
            else
                    return EC_STATE_END;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            if (fop->id == GF_FOP_READDIR) {
                if (fop->cbks.readdir != NULL) {
                    fop->cbks.readdir(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL, NULL);
                }
            } else {
                if (fop->cbks.readdirp != NULL) {
                    fop->cbks.readdirp(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL, NULL);
                }
            }
            if (fop->offset == 0)
                    return EC_STATE_LOCK_REUSE;
            else
                    return EC_STATE_END;

        case -EC_STATE_LOCK_REUSE:
        case EC_STATE_LOCK_REUSE:
            GF_ASSERT (fop->offset == 0);
            ec_lock_reuse(fop);

            return EC_STATE_UNLOCK;

        case -EC_STATE_UNLOCK:
        case EC_STATE_UNLOCK:
            GF_ASSERT (fop->offset == 0);
            ec_unlock(fop);

            return EC_STATE_END;
        default:
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(READDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READDIR,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_readdir, ec_manager_readdir, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
    fop->offset = offset;

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
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}

/* FOP: readdirp */

void ec_wind_readdirp(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_common_readdir_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->readdirp,
                      fop->fd, fop->size, fop->offset, fop->xdata);
}

void ec_readdirp(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_readdirp_cbk_t func, void * data,
                 fd_t * fd, size_t size, off_t offset, dict_t * xdata)
{
    ec_cbk_t callback = { .readdirp = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(READDIRP) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READDIRP,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_readdirp, ec_manager_readdir, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
    fop->offset = offset;

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
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}
