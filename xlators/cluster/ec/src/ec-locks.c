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

#define EC_LOCK_MODE_NONE 0
#define EC_LOCK_MODE_INC  1
#define EC_LOCK_MODE_ALL  2

int32_t ec_lock_check(ec_fop_data_t * fop, ec_cbk_data_t ** cbk,
                      uintptr_t * mask)
{
    ec_t * ec = fop->xl->private;
    ec_cbk_data_t * ans = NULL;
    uintptr_t locked = 0, notlocked = 0;
    int32_t error = -1;

    list_for_each_entry(ans, &fop->cbk_list, list)
    {
        if (ans->op_ret >= 0)
        {
            if (locked != 0)
            {
                error = EIO;
            }
            locked |= ans->mask;
            *cbk = ans;
        }
        else if (ans->op_errno == EAGAIN)
        {
            notlocked |= ans->mask;
        }
    }

    if (error == -1)
    {
        if (ec_bits_count(locked | notlocked) >= ec->fragments)
        {
            if (notlocked == 0)
            {
                fop->answer = *cbk;

                ec_update_bad(fop, locked);

                error = 0;
            }
            else
            {
                if (fop->uint32 == EC_LOCK_MODE_NONE)
                {
                    error = EAGAIN;
                }
                else
                {
                    fop->uint32 = EC_LOCK_MODE_INC;
                }
            }
        }
        else
        {
            error = EIO;
        }
    }

    *mask = locked;

    return error;
}

uintptr_t ec_lock_handler(ec_fop_data_t * fop, ec_cbk_data_t * cbk,
                          ec_combine_f combine)
{
    uintptr_t mask = 0;

    if (fop->uint32 == EC_LOCK_MODE_INC)
    {
        if (cbk->op_ret < 0)
        {
            if (cbk->op_errno != ENOTCONN)
            {
                mask = fop->mask & ~fop->remaining & ~cbk->mask;
                fop->remaining = 0;
            }
        }
    }

    ec_combine(cbk, combine);

    return mask;
}

int32_t ec_lock_unlocked(call_frame_t * frame, void * cookie,
                         xlator_t * this, int32_t op_ret, int32_t op_errno,
                         dict_t * xdata)
{
    if (op_ret < 0)
    {
        gf_log(this->name, GF_LOG_WARNING, "Failed to unlock an entry/inode");
    }

    return 0;
}

int32_t ec_lock_lk_unlocked(call_frame_t * frame, void * cookie,
                            xlator_t * this, int32_t op_ret, int32_t op_errno,
                            struct gf_flock * flock, dict_t * xdata)
{
    if (op_ret < 0)
    {
        gf_log(this->name, GF_LOG_WARNING, "Failed to unlock an lk");
    }

    return 0;
}

/* FOP: entrylk */

int32_t ec_entrylk_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_ENTRYLK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        uintptr_t mask;

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

        mask = ec_lock_handler(fop, cbk, NULL);
        if (mask != 0)
        {
            ec_entrylk(fop->req_frame, fop->xl, mask, 1, ec_lock_unlocked,
                       NULL, fop->str[0], &fop->loc[0], fop->str[1],
                       ENTRYLK_UNLOCK, fop->entrylk_type, fop->xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_entrylk(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_entrylk_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->entrylk,
                      fop->str[0], &fop->loc[0], fop->str[1], fop->entrylk_cmd,
                      fop->entrylk_type, fop->xdata);
}

int32_t ec_manager_entrylk(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            if (fop->entrylk_cmd == ENTRYLK_LOCK)
            {
                fop->uint32 = EC_LOCK_MODE_ALL;
                fop->entrylk_cmd = ENTRYLK_LOCK_NB;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if ((cbk == NULL) ||
                ((cbk->op_ret < 0) && (cbk->op_errno == EAGAIN)))
            {
                uintptr_t mask;

                fop->error = ec_lock_check(fop, &cbk, &mask);
                if (fop->error != 0)
                {
                    if (mask != 0)
                    {
                        if (fop->id == GF_FOP_ENTRYLK)
                        {
                            ec_entrylk(fop->req_frame, fop->xl, mask, 1,
                                       ec_lock_unlocked, NULL, fop->str[0],
                                       &fop->loc[0], fop->str[1],
                                       ENTRYLK_UNLOCK, fop->entrylk_type,
                                       fop->xdata);
                        }
                        else
                        {
                            ec_fentrylk(fop->req_frame, fop->xl, mask, 1,
                                        ec_lock_unlocked, NULL, fop->str[0],
                                        fop->fd, fop->str[1], ENTRYLK_UNLOCK,
                                        fop->entrylk_type, fop->xdata);
                        }
                    }
                    if (fop->error > 0)
                    {
                        return EC_STATE_REPORT;
                    }

                    fop->error = 0;

                    fop->entrylk_cmd = ENTRYLK_LOCK;

                    ec_dispatch_inc(fop);

                    return EC_STATE_PREPARE_ANSWER;
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == GF_FOP_ENTRYLK)
            {
                if (fop->cbks.entrylk != NULL)
                {
                    fop->cbks.entrylk(fop->req_frame, fop, fop->xl,
                                      cbk->op_ret, cbk->op_errno, cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.fentrylk != NULL)
                {
                    fop->cbks.fentrylk(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno, cbk->xdata);
                }
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_ENTRYLK)
            {
                if (fop->cbks.entrylk != NULL)
                {
                    fop->cbks.entrylk(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL);
                }
            }
            else
            {
                if (fop->cbks.fentrylk != NULL)
                {
                    fop->cbks.fentrylk(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL);
                }
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_entrylk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_entrylk_cbk_t func, void * data,
                const char * volume, loc_t * loc, const char * basename,
                entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    ec_cbk_t callback = { .entrylk = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(ENTRYLK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_ENTRYLK,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_entrylk, ec_manager_entrylk, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->entrylk_cmd = cmd;
    fop->entrylk_type = type;

    if (volume != NULL)
    {
        fop->str[0] = gf_strdup(volume);
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
    if (basename != NULL)
    {
        fop->str[1] = gf_strdup(basename);
        if (fop->str[1] == NULL)
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

/* FOP: fentrylk */

int32_t ec_fentrylk_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FENTRYLK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        uintptr_t mask;

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

        mask = ec_lock_handler(fop, cbk, NULL);
        if (mask != 0)
        {
            ec_fentrylk(fop->req_frame, fop->xl, mask, 1, ec_lock_unlocked,
                        NULL, fop->str[0], fop->fd, fop->str[1],
                        ENTRYLK_UNLOCK, fop->entrylk_type, fop->xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_fentrylk(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_fentrylk_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->fentrylk,
                      fop->str[0], fop->fd, fop->str[1], fop->entrylk_cmd,
                      fop->entrylk_type, fop->xdata);
}

void ec_fentrylk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_fentrylk_cbk_t func, void * data,
                 const char * volume, fd_t * fd, const char * basename,
                 entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    ec_cbk_t callback = { .fentrylk = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FENTRYLK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FENTRYLK,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_fentrylk, ec_manager_entrylk, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->entrylk_cmd = cmd;
    fop->entrylk_type = type;

    if (volume != NULL)
    {
        fop->str[0] = gf_strdup(volume);
        if (fop->str[0] == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a string.");

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
    if (basename != NULL)
    {
        fop->str[1] = gf_strdup(basename);
        if (fop->str[1] == NULL)
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

/* FOP: inodelk */

int32_t ec_inodelk_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_INODELK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        uintptr_t mask;

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

        mask = ec_lock_handler(fop, cbk, NULL);
        if (mask != 0)
        {
            ec_t * ec = fop->xl->private;
            struct gf_flock flock;

            flock.l_type = F_UNLCK;
            flock.l_whence = fop->flock.l_whence;
            flock.l_start = fop->flock.l_start * ec->fragments;
            flock.l_len = fop->flock.l_len * ec->fragments;
            flock.l_pid = 0;
            flock.l_owner.len = 0;

            ec_inodelk(fop->req_frame, fop->xl, mask, 1, ec_lock_unlocked,
                       NULL, fop->str[0], &fop->loc[0], F_SETLK, &flock,
                       fop->xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_inodelk(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_inodelk_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->inodelk,
                      fop->str[0], &fop->loc[0], fop->int32, &fop->flock,
                      fop->xdata);
}

int32_t ec_manager_inodelk(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            fop->flock.l_len += ec_adjust_offset(fop->xl->private,
                                                 &fop->flock.l_start, 1);
            fop->flock.l_len = ec_adjust_size(fop->xl->private,
                                              fop->flock.l_len, 1);
            if ((fop->int32 == F_SETLKW) && (fop->flock.l_type != F_UNLCK))
            {
                fop->uint32 = EC_LOCK_MODE_ALL;
                fop->int32 = F_SETLK;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if ((cbk == NULL) ||
                ((cbk->op_ret < 0) && (cbk->op_errno == EAGAIN)))
            {
                uintptr_t mask;

                fop->error = ec_lock_check(fop, &cbk, &mask);
                if (fop->error != 0)
                {
                    if (mask != 0)
                    {
                        ec_t * ec = fop->xl->private;
                        struct gf_flock flock;

                        flock.l_type = F_UNLCK;
                        flock.l_whence = fop->flock.l_whence;
                        flock.l_start = fop->flock.l_start * ec->fragments;
                        flock.l_len = fop->flock.l_len * ec->fragments;
                        flock.l_pid = 0;
                        flock.l_owner.len = 0;

                        if (fop->id == GF_FOP_INODELK)
                        {
                            ec_inodelk(fop->req_frame, fop->xl, mask, 1,
                                       ec_lock_unlocked, NULL, fop->str[0],
                                       &fop->loc[0], F_SETLK, &flock,
                                       fop->xdata);
                        }
                        else
                        {
                            ec_finodelk(fop->req_frame, fop->xl, mask, 1,
                                        ec_lock_unlocked, NULL, fop->str[0],
                                        fop->fd, F_SETLK, &flock, fop->xdata);
                        }
                    }
                    if (fop->error > 0)
                    {
                        return EC_STATE_REPORT;
                    }

                    fop->error = 0;

                    fop->int32 = F_SETLKW;

                    ec_dispatch_inc(fop);

                    return EC_STATE_PREPARE_ANSWER;
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->id == GF_FOP_INODELK)
            {
                if (fop->cbks.inodelk != NULL)
                {
                    fop->cbks.inodelk(fop->req_frame, fop, fop->xl,
                                      cbk->op_ret, cbk->op_errno, cbk->xdata);
                }
            }
            else
            {
                if (fop->cbks.finodelk != NULL)
                {
                    fop->cbks.finodelk(fop->req_frame, fop, fop->xl,
                                       cbk->op_ret, cbk->op_errno, cbk->xdata);
                }
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->id == GF_FOP_INODELK)
            {
                if (fop->cbks.inodelk != NULL)
                {
                    fop->cbks.inodelk(fop->req_frame, fop, fop->xl, -1,
                                      fop->error, NULL);
                }
            }
            else
            {
                if (fop->cbks.finodelk != NULL)
                {
                    fop->cbks.finodelk(fop->req_frame, fop, fop->xl, -1,
                                       fop->error, NULL);
                }
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_inodelk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                int32_t minimum, fop_inodelk_cbk_t func, void * data,
                const char * volume, loc_t * loc, int32_t cmd,
                struct gf_flock * flock, dict_t * xdata)
{
    ec_cbk_t callback = { .inodelk = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(INODELK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_INODELK,
                               EC_FLAG_UPDATE_LOC_INODE, target, minimum,
                               ec_wind_inodelk, ec_manager_inodelk, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->int32 = cmd;

    if (volume != NULL)
    {
        fop->str[0] = gf_strdup(volume);
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
    if (flock != NULL)
    {
        fop->flock.l_type = flock->l_type;
        fop->flock.l_whence = flock->l_whence;
        fop->flock.l_start = flock->l_start;
        fop->flock.l_len = flock->l_len;
        fop->flock.l_pid = flock->l_pid;
        fop->flock.l_owner.len = flock->l_owner.len;
        if (flock->l_owner.len > 0)
        {
            memcpy(fop->flock.l_owner.data, flock->l_owner.data,
                   flock->l_owner.len);
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

/* FOP: finodelk */

int32_t ec_finodelk_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_FINODELK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        uintptr_t mask;

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

        mask = ec_lock_handler(fop, cbk, NULL);
        if (mask != 0)
        {
            ec_t * ec = fop->xl->private;
            struct gf_flock flock;

            flock.l_type = F_UNLCK;
            flock.l_whence = fop->flock.l_whence;
            flock.l_start = fop->flock.l_start * ec->fragments;
            flock.l_len = fop->flock.l_len * ec->fragments;
            flock.l_pid = 0;
            flock.l_owner.len = 0;

            ec_finodelk(fop->req_frame, fop->xl, mask, 1, ec_lock_unlocked,
                        NULL, fop->str[0], fop->fd, F_SETLK, &flock,
                        fop->xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_finodelk(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_finodelk_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->finodelk,
                      fop->str[0], fop->fd, fop->int32, &fop->flock,
                      fop->xdata);
}

void ec_finodelk(call_frame_t * frame, xlator_t * this, uintptr_t target,
                 int32_t minimum, fop_finodelk_cbk_t func, void * data,
                 const char * volume, fd_t * fd, int32_t cmd,
                 struct gf_flock * flock, dict_t * xdata)
{
    ec_cbk_t callback = { .finodelk = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(FINODELK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_FINODELK,
                               EC_FLAG_UPDATE_FD_INODE, target, minimum,
                               ec_wind_finodelk, ec_manager_inodelk, callback,
                               data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = cmd;

    if (volume != NULL)
    {
        fop->str[0] = gf_strdup(volume);
        if (fop->str[0] == NULL)
        {
            gf_log(this->name, GF_LOG_ERROR, "Failed to duplicate a string.");

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
    if (flock != NULL)
    {
        fop->flock.l_type = flock->l_type;
        fop->flock.l_whence = flock->l_whence;
        fop->flock.l_start = flock->l_start;
        fop->flock.l_len = flock->l_len;
        fop->flock.l_pid = flock->l_pid;
        fop->flock.l_owner.len = flock->l_owner.len;
        if (flock->l_owner.len > 0)
        {
            memcpy(fop->flock.l_owner.data, flock->l_owner.data,
                   flock->l_owner.len);
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

/* FOP: lk */

int32_t ec_combine_lk(ec_fop_data_t * fop, ec_cbk_data_t * dst,
                      ec_cbk_data_t * src)
{
    if (!ec_flock_compare(&dst->flock, &src->flock))
    {
        gf_log(fop->xl->name, GF_LOG_NOTICE, "Mismatching lock in "
                                             "answers of 'GF_FOP_LK'");

        return 0;
    }

    return 1;
}

int32_t ec_lk_cbk(call_frame_t * frame, void * cookie, xlator_t * this,
                  int32_t op_ret, int32_t op_errno, struct gf_flock * flock,
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

    cbk = ec_cbk_data_allocate(frame, this, fop, GF_FOP_LK, idx, op_ret,
                               op_errno);
    if (cbk != NULL)
    {
        uintptr_t mask;

        if (op_ret >= 0)
        {
            if (flock != NULL)
            {
                cbk->flock.l_type = flock->l_type;
                cbk->flock.l_whence = flock->l_whence;
                cbk->flock.l_start = flock->l_start;
                cbk->flock.l_len = flock->l_len;
                cbk->flock.l_pid = flock->l_pid;
                cbk->flock.l_owner.len = flock->l_owner.len;
                if (flock->l_owner.len > 0)
                {
                    memcpy(cbk->flock.l_owner.data, flock->l_owner.data,
                           flock->l_owner.len);
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

        mask = ec_lock_handler(fop, cbk, ec_combine_lk);
        if (mask != 0)
        {
            ec_t * ec = fop->xl->private;
            struct gf_flock flock;

            flock.l_type = F_UNLCK;
            flock.l_whence = fop->flock.l_whence;
            flock.l_start = fop->flock.l_start * ec->fragments;
            flock.l_len = fop->flock.l_len * ec->fragments;
            flock.l_pid = 0;
            flock.l_owner.len = 0;

            ec_lk(fop->req_frame, fop->xl, mask, 1, ec_lock_lk_unlocked, NULL,
                  fop->fd, F_SETLK, &flock, fop->xdata);
        }
    }

out:
    if (fop != NULL)
    {
        ec_complete(fop);
    }

    return 0;
}

void ec_wind_lk(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    ec_trace("WIND", fop, "idx=%d", idx);

    STACK_WIND_COOKIE(fop->frame, ec_lk_cbk, (void *)(uintptr_t)idx,
                      ec->xl_list[idx], ec->xl_list[idx]->fops->lk, fop->fd,
                      fop->int32, &fop->flock, fop->xdata);
}

int32_t ec_manager_lk(ec_fop_data_t * fop, int32_t state)
{
    ec_cbk_data_t * cbk;

    switch (state)
    {
        case EC_STATE_INIT:
            fop->flock.l_len += ec_adjust_offset(fop->xl->private,
                                                 &fop->flock.l_start, 1);
            fop->flock.l_len = ec_adjust_size(fop->xl->private,
                                              fop->flock.l_len, 1);
            if ((fop->int32 == F_SETLKW) && (fop->flock.l_type != F_UNLCK))
            {
                fop->uint32 = EC_LOCK_MODE_ALL;
                fop->int32 = F_SETLK;
            }

        /* Fall through */

        case EC_STATE_DISPATCH:
            ec_dispatch_all(fop);

            return EC_STATE_PREPARE_ANSWER;

        case EC_STATE_PREPARE_ANSWER:
            cbk = fop->answer;
            if ((cbk == NULL) ||
                ((cbk->op_ret < 0) && (cbk->op_errno == EAGAIN)))
            {
                uintptr_t mask;

                fop->error = ec_lock_check(fop, &cbk, &mask);
                if (fop->error != 0)
                {
                    if (mask != 0)
                    {
                        ec_t * ec = fop->xl->private;
                        struct gf_flock flock;

                        flock.l_type = F_UNLCK;
                        flock.l_whence = fop->flock.l_whence;
                        flock.l_start = fop->flock.l_start * ec->fragments;
                        flock.l_len = fop->flock.l_len * ec->fragments;
                        flock.l_pid = 0;
                        flock.l_owner.len = 0;

                        ec_lk(fop->req_frame, fop->xl, mask, 1,
                              ec_lock_lk_unlocked, NULL, fop->fd, F_SETLK,
                              &flock, fop->xdata);
                    }
                    if (fop->error > 0)
                    {
                        return EC_STATE_REPORT;
                    }

                    fop->error = 0;

                    fop->int32 = F_SETLKW;

                    ec_dispatch_inc(fop);

                    return EC_STATE_PREPARE_ANSWER;
                }
            }

            return EC_STATE_REPORT;

        case EC_STATE_REPORT:
            cbk = fop->answer;

            GF_ASSERT(cbk != NULL);

            if (fop->cbks.lk != NULL)
            {
                fop->cbks.lk(fop->req_frame, fop, fop->xl, cbk->op_ret,
                             cbk->op_errno, &cbk->flock, cbk->xdata);
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        case -EC_STATE_DISPATCH:
        case -EC_STATE_PREPARE_ANSWER:
        case -EC_STATE_REPORT:
            GF_ASSERT(fop->error != 0);

            if (fop->cbks.lk != NULL)
            {
                fop->cbks.lk(fop->req_frame, fop, fop->xl, -1, fop->error,
                             NULL, NULL);
            }

            ec_wait_winds(fop);

            return EC_STATE_END;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Unhandled state %d for %s",
                   state, ec_fop_name(fop->id));

            return EC_STATE_END;
    }
}

void ec_lk(call_frame_t * frame, xlator_t * this, uintptr_t target,
           int32_t minimum, fop_lk_cbk_t func, void * data, fd_t * fd,
           int32_t cmd, struct gf_flock * flock, dict_t * xdata)
{
    ec_cbk_t callback = { .lk = func };
    ec_fop_data_t * fop = NULL;
    int32_t error = EIO;

    gf_log("ec", GF_LOG_TRACE, "EC(LK) %p", frame);

    VALIDATE_OR_GOTO(this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    fop = ec_fop_data_allocate(frame, this, GF_FOP_LK, EC_FLAG_UPDATE_FD_INODE,
                               target, minimum, ec_wind_lk, ec_manager_lk,
                               callback, data);
    if (fop == NULL)
    {
        goto out;
    }

    fop->use_fd = 1;

    fop->int32 = cmd;

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
    if (flock != NULL)
    {
        fop->flock.l_type = flock->l_type;
        fop->flock.l_whence = flock->l_whence;
        fop->flock.l_start = flock->l_start;
        fop->flock.l_len = flock->l_len;
        fop->flock.l_pid = flock->l_pid;
        fop->flock.l_owner.len = flock->l_owner.len;
        if (flock->l_owner.len > 0)
        {
            memcpy(fop->flock.l_owner.data, flock->l_owner.data,
                   flock->l_owner.len);
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
