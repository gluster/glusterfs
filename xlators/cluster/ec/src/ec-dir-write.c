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

int
ec_dir_write_cbk (call_frame_t *frame, xlator_t *this,
                  void *cookie, int op_ret, int op_errno,
                  struct iatt *poststat, struct iatt *preparent,
                  struct iatt *postparent, struct iatt *preparent2,
                  struct iatt *postparent2, dict_t *xdata)
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
        idx = (long) cookie;

        ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
                 frame, op_ret, op_errno);

        cbk = ec_cbk_data_allocate (frame, this, fop, fop->id, idx, op_ret,
                                    op_errno);
        if (!cbk)
                goto out;

        if (xdata)
                cbk->xdata = dict_ref (xdata);

        if (op_ret < 0)
                goto out;

        if (poststat)
                cbk->iatt[i++] = *poststat;

        if (preparent)
                cbk->iatt[i++] = *preparent;

        if (postparent)
                cbk->iatt[i++] = *postparent;

        if (preparent2)
                cbk->iatt[i++] = *preparent2;

        if (postparent2)
                cbk->iatt[i++] = *postparent2;

out:
        if (cbk)
                ec_combine (cbk, ec_combine_write);
        if (fop)
                ec_complete (fop);
        return 0;
}

/* FOP: create */

int32_t ec_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preparent, postparent, NULL, NULL, xdata);
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
    ec_config_t config;
    ec_t *ec;
    ec_cbk_data_t *cbk;
    ec_fd_t *ctx;
    uint64_t version[2] = {0, 0};
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

            ctx->flags = fop->int32;

            UNLOCK(&fop->fd->lock);

            if (fop->xdata == NULL) {
                fop->xdata = dict_new();
                if (fop->xdata == NULL) {
                    fop->error = ENOMEM;

                    return EC_STATE_REPORT;
                }
            }

            ec = fop->xl->private;

            config.version = EC_CONFIG_VERSION;
            config.algorithm = EC_CONFIG_ALGORITHM;
            config.gf_word_size = EC_GF_BITS;
            config.bricks = ec->nodes;
            config.redundancy = ec->redundancy;
            config.chunk_size = EC_METHOD_CHUNK_SIZE;

            err = ec_dict_set_config(fop->xdata, EC_XATTR_CONFIG, &config);
            if (err != 0) {
                fop->error = -err;

                return EC_STATE_REPORT;
            }
            err = ec_dict_set_array(fop->xdata, EC_XATTR_VERSION, version,
                                    EC_VERSION_SIZE);
            if (err != 0) {
                fop->error = -err;

                return EC_STATE_REPORT;
            }
            err = ec_dict_set_number(fop->xdata, EC_XATTR_SIZE, 0);
            if (err != 0) {
                fop->error = -err;

                return EC_STATE_REPORT;
            }

            /* We need to write to specific offsets on the bricks, so we
             * need to remove O_APPEND from flags (if present) */
            fop->int32 &= ~O_APPEND;

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3, cbk->count);

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode,
                                    &cbk->iatt[0]);
                if (!ec_cbk_set_error(cbk, -err, _gf_false)) {
                    LOCK(&fop->fd->lock);

                    ctx = __ec_fd_get(fop->fd, fop->xl);
                    if (ctx != NULL) {
                        ctx->open |= cbk->mask;
                    }

                    UNLOCK(&fop->fd->lock);
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.create != NULL)
            {
                fop->cbks.create (fop->req_frame, fop, fop->xl, cbk->op_ret,
                                  cbk->op_errno, fop->fd, fop->loc[0].inode,
                                  &cbk->iatt[0], &cbk->iatt[1], &cbk->iatt[2],
                                  cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(CREATE) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_CREATE, 0, target, minimum,
                               ec_wind_create, ec_manager_create, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = flags;
    fop->mode[0] = mode;
    fop->mode[1] = umask;

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
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: link */

int32_t ec_link_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                    int32_t op_ret, int32_t op_errno, inode_t * inode,
                    struct iatt * buf, struct iatt * preparent,
                    struct iatt * postparent, dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preparent, postparent, NULL, NULL, xdata);
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
            ec_lock_prepare_parent_inode(fop, &fop->loc[1], &fop->loc[0],
                                         EC_UPDATE_DATA | EC_UPDATE_META |
                                         EC_INODE_SIZE);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3, cbk->count);

                if (cbk->iatt[0].ia_type == IA_IFREG) {
                    cbk->iatt[0].ia_size = fop->locks[0].size;
                }

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode,
                                    &cbk->iatt[0]);
                ec_cbk_set_error(cbk, -err, _gf_false);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.link != NULL)
            {
                fop->cbks.link(fop->req_frame, fop, fop->xl, cbk->op_ret,
                               cbk->op_errno, fop->loc[0].inode, &cbk->iatt[0],
                               &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(LINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_LINK, 0, target, minimum,
                               ec_wind_link, ec_manager_link, callback, data);
    if (fop == NULL) {
        goto out;
    }

    if (oldloc != NULL) {
        if (loc_copy(&fop->loc[0], oldloc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (newloc != NULL) {
        if (loc_copy(&fop->loc[1], newloc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: mkdir */

int32_t ec_mkdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, inode_t * inode,
                     struct iatt * buf, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preparent, postparent, NULL, NULL, xdata);
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
    uint64_t version[2] = {0, 0};
    int32_t err;

    switch (state)
    {
        case EC_STATE_INIT:
            if (fop->xdata == NULL) {
                fop->xdata = dict_new();
                if (fop->xdata == NULL) {
                    fop->error = ENOMEM;

                    return EC_STATE_REPORT;
                }
            }

            err = ec_dict_set_array(fop->xdata, EC_XATTR_VERSION, version,
                                    EC_VERSION_SIZE);
            if (err != 0) {
                fop->error = -err;
                return EC_STATE_REPORT;
            }

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3, cbk->count);

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode,
                                    &cbk->iatt[0]);
                ec_cbk_set_error(cbk, -err, _gf_false);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.mkdir != NULL)
            {
                fop->cbks.mkdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, fop->loc[0].inode, &cbk->iatt[0],
                                &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            cbk = fop->answer;
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.mkdir != NULL)
            {
                fop->cbks.mkdir(fop->req_frame, fop, fop->xl, -1, fop->error,
                                NULL, NULL, NULL, NULL,
                                ((cbk) ? cbk->xdata : NULL));
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

void ec_mkdir(call_frame_t * frame, xlator_t * this, uintptr_t target,
              int32_t minimum, fop_mkdir_cbk_t func, void * data, loc_t * loc,
              mode_t mode, mode_t umask, dict_t * xdata)
{
    ec_cbk_t callback = { .mkdir = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(MKDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_MKDIR, 0, target, minimum,
                               ec_wind_mkdir, ec_manager_mkdir, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->mode[0] = mode;
    fop->mode[1] = umask;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: mknod */

int32_t ec_mknod_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, inode_t * inode,
                     struct iatt * buf, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preparent, postparent, NULL, NULL, xdata);
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
    ec_config_t config;
    ec_t *ec;
    ec_cbk_data_t * cbk;
    uint64_t version[2] = {0, 0};

    switch (state)
    {
        case EC_STATE_INIT:
            if (S_ISREG(fop->mode[0])) {
                int32_t err;

                if (fop->xdata == NULL) {
                    fop->xdata = dict_new();
                    if (fop->xdata == NULL) {
                        fop->error = ENOMEM;

                        return EC_STATE_REPORT;
                    }
                }

                ec = fop->xl->private;

                config.version = EC_CONFIG_VERSION;
                config.algorithm = EC_CONFIG_ALGORITHM;
                config.gf_word_size = EC_GF_BITS;
                config.bricks = ec->nodes;
                config.redundancy = ec->redundancy;
                config.chunk_size = EC_METHOD_CHUNK_SIZE;

                err = ec_dict_set_config(fop->xdata, EC_XATTR_CONFIG, &config);
                if (err != 0) {
                    fop->error = -err;

                    return EC_STATE_REPORT;
                }
                err = ec_dict_set_array(fop->xdata, EC_XATTR_VERSION, version,
                                        EC_VERSION_SIZE);
                if (err != 0) {
                    fop->error = -err;

                    return EC_STATE_REPORT;
                }
                err = ec_dict_set_number(fop->xdata, EC_XATTR_SIZE, 0);
                if (err != 0) {
                    fop->error = -err;

                    return EC_STATE_REPORT;
                }
            }

        /* Fall through */

        case EC_STATE_LOCK:
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3, cbk->count);

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode,
                                    &cbk->iatt[0]);
                ec_cbk_set_error(cbk, -err, _gf_false);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.mknod != NULL)
            {
                fop->cbks.mknod(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                cbk->op_errno, fop->loc[0].inode, &cbk->iatt[0],
                                &cbk->iatt[1], &cbk->iatt[2], cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(MKNOD) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_MKNOD, 0, target, minimum,
                               ec_wind_mknod, ec_manager_mknod, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->mode[0] = mode;
    fop->dev = rdev;
    fop->mode[1] = umask;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: rename */

int32_t ec_rename_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, struct iatt * buf,
                      struct iatt * preoldparent, struct iatt * postoldparent,
                      struct iatt * prenewparent, struct iatt * postnewparent,
                      dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preoldparent, postoldparent, prenewparent,
                                 postnewparent, xdata);
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
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], &fop->loc[0],
                                         EC_UPDATE_DATA | EC_UPDATE_META |
                                         EC_INODE_SIZE);
            ec_lock_prepare_parent_inode(fop, &fop->loc[1], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 5, cbk->count);

                if (cbk->iatt[0].ia_type == IA_IFREG) {
                    cbk->iatt[0].ia_size = fop->locks[0].size;
                }
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

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(RENAME) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_RENAME, 0, target, minimum,
                               ec_wind_rename, ec_manager_rename, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    if (oldloc != NULL) {
        if (loc_copy(&fop->loc[0], oldloc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (newloc != NULL) {
        if (loc_copy(&fop->loc[1], newloc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: rmdir */

int32_t ec_rmdir_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                     int32_t op_ret, int32_t op_errno, struct iatt * preparent,
                     struct iatt * postparent, dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno, NULL,
                                 preparent, postparent, NULL, NULL, xdata);
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
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
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

            if (fop->cbks.rmdir != NULL)
            {
                fop->cbks.rmdir(fop->req_frame, fop, fop->xl, cbk->op_ret,
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(RMDIR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_RMDIR, 0, target, minimum,
                               ec_wind_rmdir, ec_manager_rmdir, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = xflags;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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

/* FOP: symlink */

int32_t ec_symlink_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno, inode_t * inode,
                       struct iatt * buf, struct iatt * preparent,
                       struct iatt * postparent, dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno,
                                 buf, preparent, postparent, NULL, NULL, xdata);
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
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_false);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 3, cbk->count);

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->inode,
                                    &cbk->iatt[0]);
                ec_cbk_set_error(cbk, -err, _gf_false);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.symlink != NULL)
            {
                fop->cbks.symlink(fop->req_frame, fop, fop->xl, cbk->op_ret,
                                  cbk->op_errno, fop->loc[0].inode,
                                  &cbk->iatt[0], &cbk->iatt[1], &cbk->iatt[2],
                                  cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(SYMLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SYMLINK, 0, target, minimum,
                               ec_wind_symlink, ec_manager_symlink, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->mode[0] = umask;

    if (linkname != NULL) {
        fop->str[0] = gf_strdup(linkname);
        if (fop->str[0] == NULL) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_NO_MEMORY, "Failed to duplicate a string.");

            goto out;
        }
    }
    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref(xdata, NULL);
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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL, NULL, NULL);
    }
}

/* FOP: unlink */

int32_t ec_unlink_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt * preparent, struct iatt * postparent,
                      dict_t * xdata)
{
        return ec_dir_write_cbk (frame, this, cookie, op_ret, op_errno, NULL,
                                 preparent, postparent, NULL, NULL, xdata);
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
            ec_lock_prepare_parent_inode(fop, &fop->loc[0], NULL,
                                         EC_UPDATE_DATA | EC_UPDATE_META);
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

            if (fop->cbks.unlink != NULL)
            {
                fop->cbks.unlink(fop->req_frame, fop, fop->xl, cbk->op_ret,
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(UNLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_UNLINK, 0, target, minimum,
                               ec_wind_unlink, ec_manager_unlink, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = xflags;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL, "Failed to copy a location.");

            goto out;
        }
    }
    if (xdata != NULL) {
        fop->xdata = dict_copy_with_ref (xdata, NULL);
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
