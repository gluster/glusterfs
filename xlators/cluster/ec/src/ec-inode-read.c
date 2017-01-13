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

/* FOP: access */

int32_t ec_access_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                      int32_t op_ret, int32_t op_errno, dict_t * xdata)
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

    cbk = ec_cbk_data_allocate (frame, this, fop, GF_FOP_ACCESS,
                                idx, op_ret, op_errno);
    if (cbk) {
        if (xdata)
               cbk->xdata = dict_ref (xdata);
        ec_combine (cbk, NULL);
    }

out:
    if (fop != NULL)
    {
        ec_complete (fop);
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

int32_t
ec_manager_access(ec_fop_data_t *fop, int32_t state)
{
        ec_cbk_data_t *cbk = NULL;

        switch (state) {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_inode (fop, &fop->loc[0], EC_QUERY_INFO);
            ec_lock (fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_one (fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            if (ec_dispatch_one_retry(fop, NULL)) {
                return EC_STATE_DISPATCH;
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;
            GF_ASSERT (cbk);
            if (fop->cbks.access != NULL) {
                if (cbk) {
                    fop->cbks.access(fop->req_frame, fop, fop->xl,
                                     cbk->op_ret, cbk->op_errno,
                                     cbk->xdata);
                }
            }
            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            if (fop->cbks.access != NULL) {
                fop->cbks.access(fop->req_frame, fop, fop->xl, -1,
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
                    EC_MSG_UNHANDLED_STATE,
                    "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(ACCESS) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_ACCESS, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_access,
                               ec_manager_access, callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = mask;

    if (loc != NULL) {
        if (loc_copy(&fop->loc[0], loc) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                    EC_MSG_LOC_COPY_FAIL,
                    "Failed to copy a location.");

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
        func(frame, NULL, this, -1, error, NULL);
    }
}

/* FOP: getxattr */

int32_t ec_combine_getxattr(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                            ec_cbk_data_t * src)
{
    if (!ec_dict_compare(dst->dict, src->dict))
    {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_DICT_MISMATCH, "Mismatching dictionary in "
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
                    gf_msg (this->name, GF_LOG_ERROR, 0,
                            EC_MSG_DICT_REF_FAIL,
                            "Failed to reference a "
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL,
                        "Failed to reference a "
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

void
ec_handle_special_xattrs (ec_fop_data_t *fop)
{
        ec_cbk_data_t *cbk = NULL;
        /* Stime may not be available on all the bricks, so even if some of the
         * subvols succeed the operation, treat it as answer.*/
        if (fop->str[0] &&
            fnmatch (GF_XATTR_STIME_PATTERN, fop->str[0], 0) == 0) {
                if (!fop->answer || (fop->answer->op_ret < 0)) {
                        list_for_each_entry (cbk, &fop->cbk_list, list) {
                                if (cbk->op_ret >= 0) {
                                        fop->answer = cbk;
                                        break;
                                }
                        }
                }
        }
}

int32_t ec_manager_getxattr(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            /* clear-locks commands must be done without any locks acquired
               to avoid interferences. */
            if ((fop->str[0] == NULL) ||
                (strncmp(fop->str[0], GF_XATTR_CLRLK_CMD,
                         strlen(GF_XATTR_CLRLK_CMD)) != 0)) {
                if (fop->fd == NULL) {
                    ec_lock_prepare_inode(fop, &fop->loc[0], EC_QUERY_INFO);
                } else {
                    ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
                }
                ec_lock(fop);
            }

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            ec_handle_special_xattrs (fop);
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                int32_t err;

                err = ec_dict_combine(cbk, EC_COMBINE_DICT);
                if (!ec_cbk_set_error(cbk, -err, _gf_true)) {
                    if (cbk->xdata != NULL)
                            ec_filter_internal_xattrs (cbk->xdata);

                    if (cbk->dict != NULL)
                            ec_filter_internal_xattrs (cbk->dict);
                }
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

        case -EC_STATE_INIT:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE,
                    "Unhandled state %d for %s",
                    state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

int32_t ec_getxattr_heal_cbk(call_frame_t *frame, void *cookie, xlator_t *xl,
                             int32_t op_ret, int32_t op_errno, uintptr_t mask,
                             uintptr_t good, uintptr_t bad, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    fop_getxattr_cbk_t func = fop->data;
    ec_t *ec = xl->private;
    dict_t *dict = NULL;
    char *str;
    char bin1[65], bin2[65];

    if (op_ret >= 0) {
        dict = dict_new();
        if (dict == NULL) {
            op_ret = -1;
            op_errno = ENOMEM;
        } else {
            if (gf_asprintf(&str, "Good: %s, Bad: %s",
                            ec_bin(bin1, sizeof(bin1), good, ec->nodes),
                            ec_bin(bin2, sizeof(bin2), mask & ~(good | bad),
                                   ec->nodes)) < 0) {
                dict_unref(dict);
                dict = NULL;

                op_ret = -1;
                op_errno = ENOMEM;

                goto out;
            }

            if (dict_set_dynstr(dict, EC_XATTR_HEAL, str) != 0) {
                GF_FREE(str);
                dict_unref(dict);
                dict = NULL;

                op_ret = -1;
                op_errno = ENOMEM;

                goto out;
            }
        }
    }

out:
    func(frame, NULL, xl, op_ret, op_errno, dict, NULL);

    if (dict != NULL) {
        dict_unref(dict);
    }

    return 0;
}

void
ec_getxattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
             int32_t minimum, fop_getxattr_cbk_t func, void *data,
             loc_t *loc, const char *name, dict_t *xdata)
{
    ec_cbk_t callback = { .getxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(GETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    /* Special handling of an explicit self-heal request */
    if ((name != NULL) && (strcmp(name, EC_XATTR_HEAL) == 0)) {
        ec_heal(frame, this, target, EC_MINIMUM_ONE, ec_getxattr_heal_cbk,
                func, loc, 0, NULL);

        return;
    }

    fop = ec_fop_data_allocate(frame, this, GF_FOP_GETXATTR,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_getxattr, ec_manager_getxattr, callback,
                               data);
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
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL, NULL);
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
                    gf_msg (this->name, GF_LOG_ERROR, 0,
                            EC_MSG_DICT_REF_FAIL,
                            "Failed to reference a "
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL,
                        "Failed to reference a "
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

void
ec_wind_fgetxattr (ec_t *ec, ec_fop_data_t *fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fgetxattr_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fgetxattr,
                      fop->fd, fop->str[0], fop->xdata);
}

void
ec_fgetxattr (call_frame_t *frame, xlator_t *this, uintptr_t target,
              int32_t minimum, fop_fgetxattr_cbk_t func, void *data,
              fd_t *fd, const char *name, dict_t *xdata)
{
    ec_cbk_t callback = { .fgetxattr = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FGETXATTR) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FGETXATTR,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_fgetxattr, ec_manager_getxattr,
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
                    EC_MSG_NO_MEMORY, "Failed to duplicate a string.");

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
        ec_manager (fop, error);
    } else {
        func (frame, NULL, this, -1, error, NULL, NULL);
    }
}

/* FOP: open */

int32_t ec_combine_open(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                        ec_cbk_data_t * src)
{
    if (dst->fd != src->fd)
    {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_FD_MISMATCH, "Mismatching fd in answers "
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

int32_t ec_open_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *prebuf, struct iatt *postbuf,
                             dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    int32_t error = 0;

    fop = fop->data;
    if (op_ret >= 0) {
        fop->answer->iatt[0] = *postbuf;
    } else {
        error = op_errno;
    }

    ec_resume(fop, error);

    return 0;
}

int32_t ec_manager_open(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;
    ec_fd_t * ctx;
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

            /* We need to write to specific offsets on the bricks, so we
               need to remove O_APPEND from flags (if present).
               If O_TRUNC is specified, we remove it from open and an
               ftruncate will be executed later, which will correctly update
               the file size taking appropriate locks. O_TRUNC flag is saved
               into fop->uint32 to use it later.*/
            fop->uint32 = fop->int32 & O_TRUNC;
            fop->int32 &= ~(O_APPEND | O_TRUNC);

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                int32_t err;

                err = ec_loc_update(fop->xl, &fop->loc[0], cbk->fd->inode,
                                    NULL);
                if (!ec_cbk_set_error(cbk, -err, _gf_true)) {
                    LOCK(&fop->fd->lock);

                    ctx = __ec_fd_get(fop->fd, fop->xl);
                    if (ctx != NULL) {
                        ctx->open |= cbk->mask;
                    }

                    UNLOCK(&fop->fd->lock);

                    /* If O_TRUNC was specified, call ftruncate to
                       effectively trunc the file with appropriate locks
                       acquired. We don't use ctx->flags because self-heal
                       can use the same fd with different flags. */
                    if (fop->uint32 != 0) {
                        ec_sleep(fop);
                        ec_ftruncate(fop->req_frame, fop->xl, cbk->mask,
                                     fop->minimum, ec_open_truncate_cbk,
                                     fop, cbk->fd, 0, NULL);
                    }
                }
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

        case -EC_STATE_INIT:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(OPEN) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_OPEN, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_open, ec_manager_open,
                               callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->int32 = flags;

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

/* FOP: readlink */

int32_t ec_combine_readlink(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                            ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(fop, dst->iatt, src->iatt, 1)) {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_IATT_MISMATCH, "Mismatching iatt in "
                                             "answers of 'GF_FOP_READLINK'");

        return 0;
    }

    return 1;
}

int32_t
ec_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct iatt *buf, dict_t *xdata)
{
    ec_fop_data_t   *fop = NULL;
    ec_cbk_data_t   *cbk = NULL;
    int32_t         idx  = (int32_t)(uintptr_t)cookie;

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

            if (cbk->op_ret >= 0) {
                    cbk->iatt[0] = *buf;
                    cbk->str = gf_strdup (path);
                    if (!cbk->str) {
                            ec_cbk_set_error(cbk, ENOMEM, _gf_true);
                    }
            }
            ec_combine (cbk, NULL);
    }

out:
    if (fop != NULL)
        ec_complete(fop);

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
    ec_cbk_data_t *cbk = NULL;

    switch (state)
    {
        case EC_STATE_INIT:
        case EC_STATE_LOCK:
            ec_lock_prepare_inode (fop, &fop->loc[0], EC_QUERY_INFO);
            ec_lock (fop);
            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_one (fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            if (ec_dispatch_one_retry(fop, &cbk)) {
                return EC_STATE_DISPATCH;
            }

            if ((cbk != NULL) && (cbk->op_ret >= 0)) {
                ec_iatt_rebuild(fop->xl->private, &cbk->iatt[0], 1, 1);
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;
            GF_ASSERT (cbk);
            if (fop->cbks.readlink != NULL) {
                fop->cbks.readlink (fop->req_frame, fop, fop->xl, cbk->op_ret,
                                    cbk->op_errno, cbk->str, &cbk->iatt[0],
                                    cbk->xdata);
            }

            return EC_STATE_LOCK_REUSE;

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            if (fop->cbks.readlink != NULL) {
                fop->cbks.readlink(fop->req_frame, fop, fop->xl, -1,
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
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(READLINK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READLINK,
                               EC_FLAG_LOCK_SHARED, target, minimum,
                               ec_wind_readlink, ec_manager_readlink, callback,
                               data);
    if (fop == NULL) {
        goto out;
    }

    fop->size = size;

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
        func(frame, NULL, this, -1, error, NULL, NULL, NULL);
    }
}

/* FOP: readv */

int32_t ec_readv_rebuild(ec_t * ec, ec_fop_data_t * fop, ec_cbk_data_t * cbk)
{
    struct iovec vector[1];
    ec_cbk_data_t *ans = NULL;
    struct iobref *iobref = NULL;
    void *ptr;
    size_t fsize = 0, size = 0, max = 0;
    int32_t pos, err = -ENOMEM;

    if (cbk->op_ret < 0) {
        err = -cbk->op_errno;

        goto out;
    }

    /* This shouldn't fail because we have the inode locked. */
    GF_ASSERT(ec_get_inode_size(fop, fop->fd->inode, &cbk->iatt[0].ia_size));

    if (cbk->op_ret > 0) {
        void *blocks[cbk->count];
        uint32_t values[cbk->count];

        fsize = cbk->op_ret;
        size = fsize * ec->fragments;
        for (ans = cbk; ans != NULL; ans = ans->next) {
            pos = gf_bits_count(cbk->mask & ((1 << ans->idx) - 1));
            values[pos] = ans->idx + 1;
            blocks[pos] = ans->vector[0].iov_base;
            if ((ans->int32 != 1) ||
                !EC_ALIGN_CHECK(blocks[pos], EC_METHOD_WORD_SIZE)) {
                if (iobref == NULL) {
                    err = ec_buffer_alloc(ec->xl, size, &iobref, &ptr);
                    if (err != 0) {
                        goto out;
                    }
                }
                ec_iov_copy_to(ptr, ans->vector, ans->int32, 0, fsize);
                blocks[pos] = ptr;
                ptr += fsize;
            }
        }

        err = ec_buffer_alloc(ec->xl, size, &iobref, &ptr);
        if (err != 0) {
            goto out;
        }

        err = ec_method_decode(&ec->matrix, fsize, cbk->mask, values, blocks,
                               ptr);
        if (err != 0) {
            goto out;
        }

        vector[0].iov_base = ptr + fop->head;
        vector[0].iov_len = size - fop->head;

        max = fop->offset * ec->fragments + size;
        if (max > cbk->iatt[0].ia_size) {
            max = cbk->iatt[0].ia_size;
        }
        max -= fop->offset * ec->fragments + fop->head;
        if (max > fop->user_size) {
            max = fop->user_size;
        }
        size -= fop->head;
        if (size > max) {
            vector[0].iov_len -= size - max;
            size = max;
        }

        cbk->op_ret = size;
        cbk->int32 = 1;

        iobref_unref(cbk->buffers);
        cbk->buffers = iobref;

        GF_FREE(cbk->vector);
        cbk->vector = iov_dup(vector, 1);
        if (cbk->vector == NULL) {
            return -ENOMEM;
        }
    }

    return 0;

out:
    if (iobref != NULL) {
        iobref_unref(iobref);
    }

    return err;
}

int32_t ec_combine_readv(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                         ec_cbk_data_t * src)
{
    if (!ec_vector_compare(dst->vector, dst->int32, src->vector, src->int32))
    {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_VECTOR_MISMATCH, "Mismatching vector in "
                                             "answers of 'GF_FOP_READ'");

        return 0;
    }

    if (!ec_iatt_combine(fop, dst->iatt, src->iatt, 1)) {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_IATT_MISMATCH, "Mismatching iatt in "
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
    if (cbk != NULL) {
        if (op_ret >= 0) {
            cbk->int32 = count;

            if (count > 0) {
                cbk->vector = iov_dup(vector, count);
                if (cbk->vector == NULL) {
                    gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                            EC_MSG_NO_MEMORY, "Failed to duplicate a "
                                                     "vector list.");

                    goto out;
                }
                cbk->int32 = count;
            }
            if (stbuf != NULL) {
                cbk->iatt[0] = *stbuf;
            }
            if (iobref != NULL) {
                cbk->buffers = iobref_ref(iobref);
                if (cbk->buffers == NULL) {
                    gf_msg (this->name, GF_LOG_ERROR, 0,
                            EC_MSG_BUF_REF_FAIL, "Failed to reference a "
                                                     "buffer.");

                    goto out;
                }
            }
        }
        if (xdata != NULL) {
            cbk->xdata = dict_ref(xdata);
            if (cbk->xdata == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
                                                 "dictionary.");

                goto out;
            }
        }

        if ((op_ret > 0) && ((op_ret % ec->fragment_size) != 0)) {
            ec_cbk_set_error(cbk, EIO, _gf_true);
        }

        ec_combine(cbk, ec_combine_readv);
    }

out:
    if (fop != NULL) {
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
            ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_min(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                int32_t err;

                ec_iatt_rebuild(fop->xl->private, cbk->iatt, 1,
                                cbk->count);

                err = ec_readv_rebuild(fop->xl->private, fop, cbk);
                if (err != 0) {
                    ec_cbk_set_error(cbk, -err, _gf_true);
                }
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

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(READ) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_READ, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_readv,
                               ec_manager_readv, callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->size = size;
    fop->offset = offset;
    fop->uint32 = flags;

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
        func(frame, NULL, this, -1, error, NULL, 0, NULL, NULL, NULL);
    }
}

/* FOP: seek */

int32_t ec_seek_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, off_t offset,
                    dict_t *xdata)
{
    ec_fop_data_t *fop = NULL;
    ec_cbk_data_t *cbk = NULL;
    ec_t *ec = this->private;
    int32_t idx = (int32_t)(uintptr_t)cookie;

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, frame->local, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = frame->local;

    ec_trace("CBK", fop, "idx=%d, frame=%p, op_ret=%d, op_errno=%d", idx,
             frame, op_ret, op_errno);

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_SEEK, idx, op_ret,
                               op_errno);
    if (cbk != NULL) {
        if (op_ret >= 0) {
            cbk->offset = offset;
        }
        if (xdata != NULL) {
            cbk->xdata = dict_ref(xdata);
        }

        if ((op_ret > 0) && ((cbk->offset % ec->fragment_size) != 0)) {
            cbk->op_ret = -1;
            cbk->op_errno = EIO;
        }

        ec_combine(cbk, NULL);
    }

out:
    if (fop != NULL) {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_seek(ec_t *ec, ec_fop_data_t *fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_seek_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->seek, fop->fd,
                      fop->offset, fop->seek, fop->xdata);
}

int32_t ec_manager_seek(ec_fop_data_t *fop, int32_t state)
{
    ec_cbk_data_t *cbk;

    switch (state) {
    case EC_STATE_INIT:
        fop->user_size = fop->offset;
        fop->head = ec_adjust_offset(fop->xl->private, &fop->offset, 1);

    /* Fall through */

    case EC_STATE_LOCK:
        ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
        ec_lock(fop);

        return EC_STATE_DISPATCH;

    case EC_STATE_DISPATCH:
        ec_dispatch_one(fop);

        return EC_STATE_PREPARE_ANSWER;

    case EC_STATE_PREPARE_ANSWER:
        cbk = fop->answer;
        if (cbk != NULL) {
            if (ec_dispatch_one_retry(fop, &cbk)) {
                return EC_STATE_DISPATCH;
            }
            if (cbk->op_ret >= 0) {
                ec_t *ec = fop->xl->private;

                cbk->offset *= ec->fragments;
                if (cbk->offset < fop->user_size) {
                    cbk->offset = fop->user_size;
                }
            } else {
                ec_fop_set_error(fop, cbk->op_errno);
            }
        } else {
            ec_fop_set_error(fop, EIO);
        }

        return EC_STATE_REPORT;

    case EC_STATE_REPORT:
        cbk = fop->answer;

        GF_ASSERT(cbk != NULL);

        if (fop->cbks.seek != NULL) {
            fop->cbks.seek(fop->req_frame, fop, fop->xl, cbk->op_ret,
                           cbk->op_errno, cbk->offset, cbk->xdata);
        }

        return EC_STATE_LOCK_REUSE;

    case -EC_STATE_INIT:
    case -EC_STATE_LOCK:
    case -EC_STATE_DISPATCH:
    case -EC_STATE_PREPARE_ANSWER:
    case -EC_STATE_REPORT:
        GF_ASSERT(fop->error != 0);

        if (fop->cbks.seek != NULL) {
            fop->cbks.seek(fop->req_frame, fop, fop->xl, -1, fop->error, 0,
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
        gf_msg (fop->xl->name, GF_LOG_ERROR, 0,
                EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s", state,
                ec_fop_name(fop->id));

        return EC_STATE_END;
    }
}

void ec_seek(call_frame_t *frame, xlator_t *this, uintptr_t target,
              int32_t minimum, fop_seek_cbk_t func, void *data, fd_t *fd,
              off_t offset, gf_seek_what_t what, dict_t *xdata)
{
    ec_cbk_t callback = { .seek = func };
    ec_fop_data_t *fop = NULL;
    int32_t error = EIO;

    gf_msg_trace ("ec", 0, "EC(SEEK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_SEEK, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_seek,
                               ec_manager_seek, callback, data);
    if (fop == NULL) {
        goto out;
    }

    fop->use_fd = 1;

    fop->offset = offset;
    fop->seek = what;

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
        func(frame, NULL, this, -1, EIO, 0, NULL);
    }
}

/* FOP: stat */

int32_t ec_combine_stat(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                        ec_cbk_data_t * src)
{
    if (!ec_iatt_combine(fop, dst->iatt, src->iatt, 1)) {
        gf_msg (fop->xl->name, GF_LOG_NOTICE, 0,
                EC_MSG_IATT_MISMATCH, "Mismatching iatt in "
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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
            if (fop->fd == NULL) {
                ec_lock_prepare_inode(fop, &fop->loc[0], EC_QUERY_INFO);
            } else {
                ec_lock_prepare_fd(fop, fop->fd, EC_QUERY_INFO);
            }
            ec_lock(fop);

            return EC_STATE_DISPATCH;

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = ec_fop_prepare_answer(fop, _gf_true);
            if (cbk != NULL) {
                if (cbk->iatt[0].ia_type == IA_IFREG) {
                    ec_iatt_rebuild(fop->xl->private, cbk->iatt, 1,
                                    cbk->count);

                    /* This shouldn't fail because we have the inode locked. */
                    GF_ASSERT(ec_get_inode_size(fop,
                                                fop->locks[0].lock->loc.inode,
                                                &cbk->iatt[0].ia_size));
                }
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

        case -EC_STATE_INIT:
        case -EC_STATE_LOCK:
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
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_UNHANDLED_STATE, "Unhandled state %d for %s",
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(STAT) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_STAT, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_stat, ec_manager_stat,
                               callback, data);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        EC_MSG_DICT_REF_FAIL, "Failed to reference a "
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
    int32_t error = ENOMEM;

    gf_msg_trace ("ec", 0, "EC(FSTAT) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FSTAT, EC_FLAG_LOCK_SHARED,
                               target, minimum, ec_wind_fstat, ec_manager_stat,
                               callback, data);
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
        func(frame, NULL, this, -1, error, NULL, NULL);
    }
}
