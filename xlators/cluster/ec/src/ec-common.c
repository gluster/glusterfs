/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#include <glusterfs/hashfn.h>

#include "ec-mem-types.h"
#include "ec-types.h"
#include "ec-helpers.h"
#include "ec-combine.h"
#include "ec-common.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec.h"
#include "ec-messages.h"

#define EC_INVALID_INDEX UINT32_MAX

void
ec_update_fd_status(fd_t *fd, xlator_t *xl, int idx, int32_t ret_status)
{
    ec_fd_t *fd_ctx;

    if (fd == NULL)
        return;

    LOCK(&fd->lock);
    {
        fd_ctx = __ec_fd_get(fd, xl);
        if (fd_ctx) {
            if (ret_status >= 0)
                fd_ctx->fd_status[idx] = EC_FD_OPENED;
            else
                fd_ctx->fd_status[idx] = EC_FD_NOT_OPENED;
        }
    }
    UNLOCK(&fd->lock);
}

static uintptr_t
ec_fd_ctx_need_open(fd_t *fd, xlator_t *this, uintptr_t mask)
{
    int i = 0;
    int count = 0;
    ec_t *ec = NULL;
    ec_fd_t *fd_ctx = NULL;
    uintptr_t need_open = 0;

    ec = this->private;

    fd_ctx = ec_fd_get(fd, this);
    if (!fd_ctx)
        return count;

    LOCK(&fd->lock);
    {
        for (i = 0; i < ec->nodes; i++) {
            if ((fd_ctx->fd_status[i] == EC_FD_NOT_OPENED) &&
                ((ec->xl_up & (1 << i)) != 0) && ((mask & (1 << i)) != 0)) {
                fd_ctx->fd_status[i] = EC_FD_OPENING;
                need_open |= (1 << i);
                count++;
            }
        }
    }
    UNLOCK(&fd->lock);

    /* If fd needs to open on minimum number of nodes
     * then ignore fixing the fd as it has been
     * requested from heal operation.
     */
    if (count >= ec->fragments) {
        need_open = 0;
    }

    return need_open;
}

static gf_boolean_t
ec_is_fd_fixable(fd_t *fd)
{
    if (!fd || !fd->inode)
        return _gf_false;
    else if (fd_is_anonymous(fd))
        return _gf_false;
    else if (gf_uuid_is_null(fd->inode->gfid))
        return _gf_false;

    return _gf_true;
}

static void
ec_fix_open(ec_fop_data_t *fop, uintptr_t mask)
{
    uintptr_t need_open = 0;
    int ret = 0;
    int32_t flags = 0;
    loc_t loc = {
        0,
    };

    if (!ec_is_fd_fixable(fop->fd))
        goto out;

    /* Evaluate how many remote fd's to be opened */
    need_open = ec_fd_ctx_need_open(fop->fd, fop->xl, mask);
    if (need_open == 0) {
        goto out;
    }

    loc.inode = inode_ref(fop->fd->inode);
    gf_uuid_copy(loc.gfid, fop->fd->inode->gfid);
    ret = loc_path(&loc, NULL);
    if (ret < 0) {
        goto out;
    }

    flags = fop->fd->flags & (~(O_TRUNC | O_APPEND | O_CREAT | O_EXCL));
    if (IA_IFDIR == fop->fd->inode->ia_type) {
        ec_opendir(fop->frame, fop->xl, need_open,
                   EC_MINIMUM_ONE | EC_FOP_NO_PROPAGATE_ERROR, NULL, NULL,
                   &fop->loc[0], fop->fd, NULL);
    } else {
        ec_open(fop->frame, fop->xl, need_open,
                EC_MINIMUM_ONE | EC_FOP_NO_PROPAGATE_ERROR, NULL, NULL, &loc,
                flags, fop->fd, NULL);
    }

out:
    loc_wipe(&loc);
}

static off_t
ec_range_end_get(off_t fl_start, uint64_t fl_size)
{
    if (fl_size > 0) {
        if (fl_size >= EC_RANGE_FULL) {
            /* Infinity */
            fl_start = LLONG_MAX;
        } else {
            fl_start += fl_size - 1;
            if (fl_start < 0) {
                /* Overflow */
                fl_start = LLONG_MAX;
            }
        }
    }

    return fl_start;
}

static gf_boolean_t
ec_is_range_conflict(ec_lock_link_t *l1, ec_lock_link_t *l2)
{
    return ((l1->fl_end >= l2->fl_start) && (l2->fl_end >= l1->fl_start));
}

static gf_boolean_t
ec_lock_conflict(ec_lock_link_t *l1, ec_lock_link_t *l2)
{
    ec_t *ec = l1->fop->xl->private;

    /* Fops like access/stat won't have to worry what the other fops are
     * modifying as the fop is wound only to one brick. So it can be
     * executed in parallel*/
    if (l1->fop->minimum == EC_MINIMUM_ONE ||
        l2->fop->minimum == EC_MINIMUM_ONE)
        return _gf_false;

    if ((l1->fop->flags & EC_FLAG_LOCK_SHARED) &&
        (l2->fop->flags & EC_FLAG_LOCK_SHARED))
        return _gf_false;

    if (!ec->parallel_writes) {
        return _gf_true;
    }

    return ec_is_range_conflict(l1, l2);
}

uint32_t
ec_select_first_by_read_policy(ec_t *ec, ec_fop_data_t *fop)
{
    if (ec->read_policy == EC_ROUND_ROBIN) {
        return ec->idx;
    } else if (ec->read_policy == EC_GFID_HASH) {
        if (fop->use_fd) {
            return SuperFastHash((char *)fop->fd->inode->gfid,
                                 sizeof(fop->fd->inode->gfid)) %
                   ec->nodes;
        } else {
            if (gf_uuid_is_null(fop->loc[0].gfid))
                loc_gfid(&fop->loc[0], fop->loc[0].gfid);
            return SuperFastHash((char *)fop->loc[0].gfid,
                                 sizeof(fop->loc[0].gfid)) %
                   ec->nodes;
        }
    }
    return 0;
}

static gf_boolean_t
ec_child_valid(ec_t *ec, ec_fop_data_t *fop, uint32_t idx)
{
    return (idx < ec->nodes) && (((fop->remaining >> idx) & 1) == 1);
}

static uint32_t
ec_child_next(ec_t *ec, ec_fop_data_t *fop, uint32_t idx)
{
    while (!ec_child_valid(ec, fop, idx)) {
        if (++idx >= ec->nodes) {
            idx = 0;
        }
        if (idx == fop->first) {
            return EC_INVALID_INDEX;
        }
    }

    return idx;
}

int32_t
ec_heal_report(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, uintptr_t mask, uintptr_t good,
               uintptr_t bad, uint32_t pending, dict_t *xdata)
{
    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_DEBUG, op_errno, EC_MSG_HEAL_FAIL,
               "Heal failed");
    } else {
        if ((mask & ~good) != 0) {
            gf_msg(this->name, GF_LOG_DEBUG, 0, EC_MSG_HEAL_SUCCESS,
                   "Heal succeeded on %d/%d "
                   "subvolumes",
                   gf_bits_count(mask & ~(good | bad)),
                   gf_bits_count(mask & ~good));
        }
    }

    return 0;
}

static uintptr_t
ec_fop_needs_name_heal(ec_fop_data_t *fop)
{
    ec_t *ec = NULL;
    ec_cbk_data_t *cbk = NULL;
    ec_cbk_data_t *enoent_cbk = NULL;

    ec = fop->xl->private;
    if (fop->id != GF_FOP_LOOKUP)
        return 0;

    if (!fop->loc[0].name || strlen(fop->loc[0].name) == 0)
        return 0;

    list_for_each_entry(cbk, &fop->cbk_list, list)
    {
        if (cbk->op_ret < 0 && cbk->op_errno == ENOENT) {
            enoent_cbk = cbk;
            break;
        }
    }

    if (!enoent_cbk)
        return 0;

    return ec->xl_up & ~enoent_cbk->mask;
}

int32_t
ec_fop_needs_heal(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;

    if (fop->lock_count == 0) {
        /*
         * if fop->lock_count is zero that means it saw version mismatch
         * without any locks so it can't be trusted. If we launch a heal
         * based on this it will lead to INODELKs which will affect I/O
         * performance. Considering self-heal-daemon and operations on
         * the inode from client which take locks can still trigger the
         * heal we can choose to not attempt a heal when fop->lock_count
         * is zero.
         */
        return 0;
    }
    return (ec->xl_up & ~(fop->remaining | fop->good)) != 0;
}

void
ec_check_status(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;
    int32_t partial = 0;
    char str1[32], str2[32], str3[32], str4[32], str5[32];

    if (!ec_fop_needs_name_heal(fop) && !ec_fop_needs_heal(fop)) {
        return;
    }

    if (fop->answer && fop->answer->op_ret >= 0) {
        if ((fop->id == GF_FOP_LOOKUP) || (fop->id == GF_FOP_STAT) ||
            (fop->id == GF_FOP_FSTAT)) {
            partial = fop->answer->iatt[0].ia_type == IA_IFDIR;
        } else if (fop->id == GF_FOP_OPENDIR) {
            partial = 1;
        }
    }

    gf_msg(
        fop->xl->name, GF_LOG_WARNING, 0, EC_MSG_OP_FAIL_ON_SUBVOLS,
        "Operation failed on %d of %d subvolumes.(up=%s, mask=%s, "
        "remaining=%s, good=%s, bad=%s,"
        "(Least significant bit represents first client/brick of subvol), %s)",
        gf_bits_count(ec->xl_up & ~(fop->remaining | fop->good)), ec->nodes,
        ec_bin(str1, sizeof(str1), ec->xl_up, ec->nodes),
        ec_bin(str2, sizeof(str2), fop->mask, ec->nodes),
        ec_bin(str3, sizeof(str3), fop->remaining, ec->nodes),
        ec_bin(str4, sizeof(str4), fop->good, ec->nodes),
        ec_bin(str5, sizeof(str5), ec->xl_up & ~(fop->remaining | fop->good),
               ec->nodes),
        ec_msg_str(fop));
    if (fop->use_fd) {
        if (fop->fd != NULL) {
            ec_fheal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                     fop->fd, partial, NULL);
        }
    } else {
        ec_heal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                &fop->loc[0], partial, NULL);

        if (fop->loc[1].inode != NULL) {
            ec_heal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                    &fop->loc[1], partial, NULL);
        }
    }
}

void
ec_update_good(ec_fop_data_t *fop, uintptr_t good)
{
    fop->good = good;

    /* Fops that are executed only on one brick do not have enough information
     * to decide if healing is needed or not. */
    if ((fop->expected != 1) && (fop->parent == NULL)) {
        ec_check_status(fop);
    }
}

void
ec_lock_update_good(ec_lock_t *lock, ec_fop_data_t *fop)
{
    /* Fops that are executed only on one brick do not have enough information
     * to update the global mask of good bricks. */
    if (fop->expected == 1) {
        return;
    }

    /* When updating the good mask of the lock, we only take into consideration
     * those bits corresponding to the bricks where the fop has been executed.
     * Bad bricks are removed from good_mask, but once marked as bad it's never
     * set to good until the lock is released and reacquired */

    lock->good_mask &= fop->good | fop->remaining;
}

void
__ec_fop_set_error(ec_fop_data_t *fop, int32_t error)
{
    if ((error != 0) && (fop->error == 0)) {
        fop->error = error;
    }
}

void
ec_fop_set_error(ec_fop_data_t *fop, int32_t error)
{
    LOCK(&fop->lock);

    __ec_fop_set_error(fop, error);

    UNLOCK(&fop->lock);
}

gf_boolean_t
ec_cbk_set_error(ec_cbk_data_t *cbk, int32_t error, gf_boolean_t ro)
{
    if ((error != 0) && (cbk->op_ret >= 0)) {
        /* If cbk->op_errno was 0, it means that the fop succeeded and this
         * error has happened while processing the answer. If the operation was
         * read-only, there's no problem (i.e. we simply return the generated
         * error code). However if it caused a modification, we must return EIO
         * to indicate that the operation has been partially executed. */
        cbk->op_errno = ro ? error : EIO;
        cbk->op_ret = -1;

        ec_fop_set_error(cbk->fop, cbk->op_errno);
    }

    return (cbk->op_ret < 0);
}

ec_cbk_data_t *
ec_fop_prepare_answer(ec_fop_data_t *fop, gf_boolean_t ro)
{
    ec_cbk_data_t *cbk;
    int32_t err;

    cbk = fop->answer;
    if (cbk == NULL) {
        ec_fop_set_error(fop, EIO);

        return NULL;
    }

    if (cbk->op_ret < 0) {
        ec_fop_set_error(fop, cbk->op_errno);
    }

    err = ec_dict_combine(cbk, EC_COMBINE_XDATA);
    if (ec_cbk_set_error(cbk, -err, ro)) {
        return NULL;
    }

    return cbk;
}

void
ec_sleep(ec_fop_data_t *fop)
{
    LOCK(&fop->lock);

    GF_ASSERT(fop->refs > 0);
    fop->refs++;
    fop->jobs++;

    UNLOCK(&fop->lock);
}

int32_t
ec_check_complete(ec_fop_data_t *fop, ec_resume_f resume)
{
    int32_t error = -1;

    LOCK(&fop->lock);

    GF_ASSERT(fop->resume == NULL);

    if (--fop->jobs != 0) {
        ec_trace("WAIT", fop, "resume=%p", resume);

        fop->resume = resume;
    } else {
        error = fop->error;
        fop->error = 0;
    }

    UNLOCK(&fop->lock);

    return error;
}

void
ec_resume(ec_fop_data_t *fop, int32_t error)
{
    ec_resume_f resume = NULL;

    LOCK(&fop->lock);

    __ec_fop_set_error(fop, error);

    if (--fop->jobs == 0) {
        resume = fop->resume;
        fop->resume = NULL;
        if (resume != NULL) {
            ec_trace("RESUME", fop, "error=%d", error);

            if (fop->error != 0) {
                error = fop->error;
            }
            fop->error = 0;
        }
    }

    UNLOCK(&fop->lock);

    if (resume != NULL) {
        resume(fop, error);
    }

    ec_fop_data_release(fop);
}

void
ec_resume_parent(ec_fop_data_t *fop)
{
    ec_fop_data_t *parent;
    int32_t error = 0;

    parent = fop->parent;
    if (parent != NULL) {
        if ((fop->fop_flags & EC_FOP_NO_PROPAGATE_ERROR) == 0) {
            error = fop->error;
        }
        ec_trace("RESUME_PARENT", fop, "error=%u", error);
        fop->parent = NULL;
        ec_resume(parent, error);
    }
}

gf_boolean_t
ec_is_recoverable_error(int32_t op_errno)
{
    switch (op_errno) {
        case ENOTCONN:
        case ESTALE:
        case ENOENT:
        case EBADFD: /*Opened fd but brick is disconnected*/
        case EIO:    /*Backend-fs crash like XFS/ext4 etc*/
            return _gf_true;
    }
    return _gf_false;
}

void
ec_complete(ec_fop_data_t *fop)
{
    ec_cbk_data_t *cbk = NULL;
    int32_t resume = 0, update = 0;
    int healing_count = 0;

    LOCK(&fop->lock);

    ec_trace("COMPLETE", fop, "");

    if (--fop->winds == 0) {
        if (fop->answer == NULL) {
            if (!list_empty(&fop->cbk_list)) {
                cbk = list_entry(fop->cbk_list.next, ec_cbk_data_t, list);
                healing_count = gf_bits_count(cbk->mask & fop->healing);
                /* fop shouldn't be treated as success if it is not
                 * successful on at least fop->minimum good copies*/
                if ((cbk->count - healing_count) >= fop->minimum) {
                    fop->answer = cbk;

                    update = 1;
                }
            }

            resume = 1;
        }
    }

    UNLOCK(&fop->lock);

    /* ec_update_good() locks inode->lock. This may cause deadlocks with
       fop->lock when used in another order. Since ec_update_good() will not
       be called more than once for each fop, it can be called from outside
       the fop->lock locked region. */
    if (update) {
        ec_update_good(fop, cbk->mask);
    }

    if (resume) {
        ec_resume(fop, 0);
    }

    ec_fop_data_release(fop);
}

/* There could be already granted locks sitting on the bricks, unlock for which
 * must be wound at all costs*/
static gf_boolean_t
ec_must_wind(ec_fop_data_t *fop)
{
    if ((fop->id == GF_FOP_INODELK) || (fop->id == GF_FOP_FINODELK) ||
        (fop->id == GF_FOP_LK)) {
        if (fop->flock.l_type == F_UNLCK)
            return _gf_true;
    } else if ((fop->id == GF_FOP_ENTRYLK) || (fop->id == GF_FOP_FENTRYLK)) {
        if (fop->entrylk_cmd == ENTRYLK_UNLOCK)
            return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
ec_internal_op(ec_fop_data_t *fop)
{
    if (ec_must_wind(fop))
        return _gf_true;
    if (fop->id == GF_FOP_XATTROP)
        return _gf_true;
    if (fop->id == GF_FOP_FXATTROP)
        return _gf_true;
    if (fop->id == GF_FOP_OPEN)
        return _gf_true;
    return _gf_false;
}

char *
ec_msg_str(ec_fop_data_t *fop)
{
    loc_t *loc1 = NULL;
    loc_t *loc2 = NULL;
    char gfid1[64] = {0};
    char gfid2[64] = {0};
    ec_fop_data_t *parent = fop->parent;

    if (fop->errstr)
        return fop->errstr;
    if (!fop->use_fd) {
        loc1 = &fop->loc[0];
        loc2 = &fop->loc[1];

        if (fop->id == GF_FOP_RENAME) {
            gf_asprintf(&fop->errstr,
                        "FOP : '%s' failed on '%s' and '%s' with gfids "
                        "%s and %s respectively. Parent FOP: %s",
                        ec_fop_name(fop->id), loc1->path, loc2->path,
                        uuid_utoa_r(loc1->gfid, gfid1),
                        uuid_utoa_r(loc2->gfid, gfid2),
                        parent ? ec_fop_name(parent->id) : "No Parent");
        } else {
            gf_asprintf(
                &fop->errstr,
                "FOP : '%s' failed on '%s' with gfid %s. Parent FOP: %s",
                ec_fop_name(fop->id), loc1->path,
                uuid_utoa_r(loc1->gfid, gfid1),
                parent ? ec_fop_name(parent->id) : "No Parent");
        }
    } else {
        gf_asprintf(
            &fop->errstr, "FOP : '%s' failed on gfid %s. Parent FOP: %s",
            ec_fop_name(fop->id), uuid_utoa_r(fop->fd->inode->gfid, gfid1),
            parent ? ec_fop_name(parent->id) : "No Parent");
    }
    return fop->errstr;
}

static void
ec_log_insufficient_vol(ec_fop_data_t *fop, int32_t have, uint32_t need,
                        int32_t loglevel)
{
    ec_t *ec = fop->xl->private;
    char str1[32], str2[32], str3[32];

    gf_msg(ec->xl->name, loglevel, 0, EC_MSG_CHILDS_INSUFFICIENT,
           "Insufficient available children for this request: "
           "Have : %d, Need : %u : Child UP : %s "
           "Mask: %s, Healing : %s : %s ",
           have, need, ec_bin(str1, sizeof(str1), ec->xl_up, ec->nodes),
           ec_bin(str2, sizeof(str2), fop->mask, ec->nodes),
           ec_bin(str3, sizeof(str3), fop->healing, ec->nodes),
           ec_msg_str(fop));
}

static int32_t
ec_child_select(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;
    int32_t first = 0, num = 0;

    ec_fop_cleanup(fop);

    fop->mask &= ec->node_mask;
    /* Wind the fop on same subvols as parent for any internal extra fops like
     * head/tail read in case of writev fop. Unlocks shouldn't do this because
     * unlock should go on all subvols where lock is performed*/
    if (fop->parent && !ec_internal_op(fop)) {
        fop->mask &= (fop->parent->mask & ~fop->parent->healing);
        if (ec_is_data_fop(fop->id)) {
            fop->healing |= fop->parent->healing;
        }
    }

    if ((fop->mask & ~ec->xl_up) != 0) {
        gf_msg(fop->xl->name, GF_LOG_WARNING, 0, EC_MSG_OP_EXEC_UNAVAIL,
               "Executing operation with "
               "some subvolumes unavailable. (%" PRIXPTR "). %s ",
               fop->mask & ~ec->xl_up, ec_msg_str(fop));
        fop->mask &= ec->xl_up;
    }

    switch (fop->minimum) {
        case EC_MINIMUM_ALL:
            fop->minimum = gf_bits_count(fop->mask);
            if (fop->minimum >= ec->fragments) {
                break;
            }
        case EC_MINIMUM_MIN:
            fop->minimum = ec->fragments;
            break;
        case EC_MINIMUM_ONE:
            fop->minimum = 1;
    }

    if (ec->read_policy == EC_ROUND_ROBIN) {
        first = ec->idx;
        if (++first >= ec->nodes) {
            first = 0;
        }
        ec->idx = first;
    }

    num = gf_bits_count(fop->mask);
    /*Unconditionally wind on healing subvolumes*/
    fop->mask |= fop->healing;
    fop->remaining = fop->mask;
    fop->received = 0;

    ec_trace("SELECT", fop, "");

    if ((num < fop->minimum) && (num < ec->fragments)) {
        ec_log_insufficient_vol(fop, num, fop->minimum, GF_LOG_ERROR);
        return 0;
    }

    if (!fop->parent && fop->lock_count &&
        (fop->locks[0].update[EC_DATA_TXN] ||
         fop->locks[0].update[EC_METADATA_TXN])) {
        if (ec->quorum_count && (num < ec->quorum_count)) {
            ec_log_insufficient_vol(fop, num, ec->quorum_count, GF_LOG_ERROR);
            return 0;
        }
    }

    return 1;
}

void
ec_dispatch_next(ec_fop_data_t *fop, uint32_t idx)
{
    uint32_t i = EC_INVALID_INDEX;
    ec_t *ec = fop->xl->private;

    LOCK(&fop->lock);

    i = ec_child_next(ec, fop, idx);
    if (i < EC_MAX_NODES) {
        idx = i;

        fop->remaining ^= 1ULL << idx;

        ec_trace("EXECUTE", fop, "idx=%d", idx);

        fop->winds++;
        fop->refs++;
    }

    UNLOCK(&fop->lock);

    if (i < EC_MAX_NODES) {
        fop->wind(ec, fop, idx);
    }
}

void
ec_dispatch_mask(ec_fop_data_t *fop, uintptr_t mask)
{
    ec_t *ec = fop->xl->private;
    int32_t count, idx;

    count = gf_bits_count(mask);

    LOCK(&fop->lock);

    ec_trace("EXECUTE", fop, "mask=%lX", mask);

    fop->remaining ^= mask;

    fop->winds += count;
    fop->refs += count;

    UNLOCK(&fop->lock);

    idx = 0;
    while (mask != 0) {
        if ((mask & 1) != 0) {
            fop->wind(ec, fop, idx);
        }
        idx++;
        mask >>= 1;
    }
}

void
ec_dispatch_start(ec_fop_data_t *fop)
{
    fop->answer = NULL;
    fop->good = 0;

    INIT_LIST_HEAD(&fop->cbk_list);

    if (fop->lock_count > 0) {
        ec_owner_copy(fop->frame, &fop->req_frame->root->lk_owner);
    }
}

void
ec_dispatch_one(ec_fop_data_t *fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop)) {
        ec_sleep(fop);

        fop->expected = 1;
        fop->first = ec_select_first_by_read_policy(fop->xl->private, fop);

        ec_dispatch_next(fop, fop->first);
    }
}

gf_boolean_t
ec_dispatch_one_retry(ec_fop_data_t *fop, ec_cbk_data_t **cbk)
{
    ec_cbk_data_t *tmp;

    tmp = ec_fop_prepare_answer(fop, _gf_true);
    if (cbk != NULL) {
        *cbk = tmp;
    }
    if ((tmp != NULL) && (tmp->op_ret < 0) &&
        ec_is_recoverable_error(tmp->op_errno)) {
        GF_ASSERT(fop->mask & (1ULL << tmp->idx));
        fop->mask ^= (1ULL << tmp->idx);
        if (fop->mask) {
            return _gf_true;
        }
    }

    return _gf_false;
}

void
ec_dispatch_inc(ec_fop_data_t *fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop)) {
        ec_sleep(fop);

        fop->expected = gf_bits_count(fop->remaining);
        fop->first = 0;

        ec_dispatch_next(fop, 0);
    }
}

void
ec_dispatch_all(ec_fop_data_t *fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop)) {
        ec_sleep(fop);

        fop->expected = gf_bits_count(fop->remaining);
        fop->first = 0;

        ec_dispatch_mask(fop, fop->remaining);
    }
}

void
ec_dispatch_min(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;
    uintptr_t mask;
    uint32_t idx;
    int32_t count;

    ec_dispatch_start(fop);

    if (ec_child_select(fop)) {
        ec_sleep(fop);

        fop->expected = count = ec->fragments;
        fop->first = ec_select_first_by_read_policy(fop->xl->private, fop);
        idx = fop->first - 1;
        mask = 0;
        while (count-- > 0) {
            idx = ec_child_next(ec, fop, idx + 1);
            if (idx < EC_MAX_NODES)
                mask |= 1ULL << idx;
        }

        ec_dispatch_mask(fop, mask);
    }
}

void
ec_succeed_all(ec_fop_data_t *fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop)) {
        fop->expected = gf_bits_count(fop->remaining);
        fop->first = 0;

        /* Simulate a successful execution on all bricks */
        ec_trace("SUCCEED", fop, "");

        fop->good = fop->remaining;
        fop->remaining = 0;
    }
}

ec_lock_t *
ec_lock_allocate(ec_fop_data_t *fop, loc_t *loc)
{
    ec_t *ec = fop->xl->private;
    ec_lock_t *lock;
    int32_t err;

    if ((loc->inode == NULL) ||
        (gf_uuid_is_null(loc->gfid) && gf_uuid_is_null(loc->inode->gfid))) {
        gf_msg(fop->xl->name, GF_LOG_ERROR, EINVAL, EC_MSG_INVALID_INODE,
               "Trying to lock based on an invalid "
               "inode");

        __ec_fop_set_error(fop, EINVAL);

        return NULL;
    }

    lock = mem_get0(ec->lock_pool);
    if (lock != NULL) {
        lock->good_mask = UINTPTR_MAX;
        INIT_LIST_HEAD(&lock->owners);
        INIT_LIST_HEAD(&lock->waiting);
        INIT_LIST_HEAD(&lock->frozen);
        err = ec_loc_from_loc(fop->xl, &lock->loc, loc);
        if (err != 0) {
            mem_put(lock);
            lock = NULL;

            __ec_fop_set_error(fop, -err);
        }
    }

    return lock;
}

void
ec_lock_destroy(ec_lock_t *lock)
{
    loc_wipe(&lock->loc);
    if (lock->fd != NULL) {
        fd_unref(lock->fd);
    }

    mem_put(lock);
}

int32_t
ec_lock_compare(ec_lock_t *lock1, ec_lock_t *lock2)
{
    return gf_uuid_compare(lock1->loc.gfid, lock2->loc.gfid);
}

static void
ec_lock_insert(ec_fop_data_t *fop, ec_lock_t *lock, uint32_t flags, loc_t *base,
               off_t fl_start, uint64_t fl_size)
{
    ec_lock_link_t *link;

    /* This check is only prepared for up to 2 locks per fop. If more locks
     * are needed this must be changed. */
    if ((fop->lock_count > 0) &&
        (ec_lock_compare(fop->locks[0].lock, lock) < 0)) {
        fop->first_lock = fop->lock_count;
    } else {
        /* When the first lock is added to the current fop, request lock
         * counts from locks xlator to be able to determine if there is
         * contention and release the lock sooner. */
        if (fop->xdata == NULL) {
            fop->xdata = dict_new();
            if (fop->xdata == NULL) {
                ec_fop_set_error(fop, ENOMEM);
                return;
            }
        }
        if (dict_set_str(fop->xdata, GLUSTERFS_INODELK_DOM_COUNT,
                         fop->xl->name) != 0) {
            ec_fop_set_error(fop, ENOMEM);
            return;
        }
    }

    link = &fop->locks[fop->lock_count++];

    link->lock = lock;
    link->fop = fop;
    link->update[EC_DATA_TXN] = (flags & EC_UPDATE_DATA) != 0;
    link->update[EC_METADATA_TXN] = (flags & EC_UPDATE_META) != 0;
    link->base = base;
    link->fl_start = fl_start;
    link->fl_end = ec_range_end_get(fl_start, fl_size);

    lock->refs_pending++;
}

static void
ec_lock_prepare_inode_internal(ec_fop_data_t *fop, loc_t *loc, uint32_t flags,
                               loc_t *base, off_t fl_start, uint64_t fl_size)
{
    ec_lock_t *lock = NULL;
    ec_inode_t *ctx;

    if ((fop->parent != NULL) || (fop->error != 0) || (loc->inode == NULL)) {
        return;
    }

    LOCK(&loc->inode->lock);

    ctx = __ec_inode_get(loc->inode, fop->xl);
    if (ctx == NULL) {
        __ec_fop_set_error(fop, ENOMEM);

        goto unlock;
    }

    if (ctx->inode_lock != NULL) {
        lock = ctx->inode_lock;

        /* If there's another lock, make sure that it's not the same. Otherwise
         * do not insert it.
         *
         * This can only happen on renames where source and target names are
         * in the same directory. */
        if ((fop->lock_count > 0) && (fop->locks[0].lock == lock)) {
            /* Combine data/meta updates */
            fop->locks[0].update[EC_DATA_TXN] |= (flags & EC_UPDATE_DATA) != 0;
            fop->locks[0].update[EC_METADATA_TXN] |= (flags & EC_UPDATE_META) !=
                                                     0;

            /* Only one base inode is allowed per fop, so there shouldn't be
             * overwrites here. */
            if (base != NULL) {
                fop->locks[0].base = base;
            }

            goto update_query;
        }

        ec_trace("LOCK_INODELK", fop,
                 "lock=%p, inode=%p. Lock already "
                 "acquired",
                 lock, loc->inode);

        goto insert;
    }

    lock = ec_lock_allocate(fop, loc);
    if (lock == NULL) {
        goto unlock;
    }

    ec_trace("LOCK_CREATE", fop, "lock=%p", lock);

    lock->flock.l_type = F_WRLCK;
    lock->flock.l_whence = SEEK_SET;

    lock->ctx = ctx;
    ctx->inode_lock = lock;

insert:
    ec_lock_insert(fop, lock, flags, base, fl_start, fl_size);
update_query:
    lock->query |= (flags & EC_QUERY_INFO) != 0;
unlock:
    UNLOCK(&loc->inode->lock);
}

void
ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, uint32_t flags,
                      off_t fl_start, uint64_t fl_size)
{
    ec_lock_prepare_inode_internal(fop, loc, flags, NULL, fl_start, fl_size);
}

void
ec_lock_prepare_parent_inode(ec_fop_data_t *fop, loc_t *loc, loc_t *base,
                             uint32_t flags)
{
    loc_t tmp;
    int32_t err;

    if (fop->error != 0) {
        return;
    }

    err = ec_loc_parent(fop->xl, loc, &tmp);
    if (err != 0) {
        ec_fop_set_error(fop, -err);

        return;
    }

    if ((flags & EC_INODE_SIZE) != 0) {
        flags ^= EC_INODE_SIZE;
    } else {
        base = NULL;
    }

    ec_lock_prepare_inode_internal(fop, &tmp, flags, base, 0, EC_RANGE_FULL);

    loc_wipe(&tmp);
}

void
ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, uint32_t flags, off_t fl_start,
                   uint64_t fl_size)
{
    loc_t loc;
    int32_t err;

    if (fop->error != 0) {
        return;
    }

    err = ec_loc_from_fd(fop->xl, &loc, fd);
    if (err != 0) {
        ec_fop_set_error(fop, -err);

        return;
    }

    ec_lock_prepare_inode_internal(fop, &loc, flags, NULL, fl_start, fl_size);

    loc_wipe(&loc);
}

gf_boolean_t
ec_config_check(xlator_t *xl, ec_config_t *config)
{
    ec_t *ec;

    ec = xl->private;
    if ((config->version != EC_CONFIG_VERSION) ||
        (config->algorithm != EC_CONFIG_ALGORITHM) ||
        (config->gf_word_size != EC_GF_BITS) || (config->bricks != ec->nodes) ||
        (config->redundancy != ec->redundancy) ||
        (config->chunk_size != EC_METHOD_CHUNK_SIZE)) {
        uint32_t data_bricks;

        /* This combination of version/algorithm requires the following
           values. Incorrect values for these fields are a sign of
           corruption:

             redundancy > 0
             redundancy * 2 < bricks
             gf_word_size must be a power of 2
             chunk_size (in bits) must be a multiple of gf_word_size *
                 (bricks - redundancy) */

        data_bricks = config->bricks - config->redundancy;
        if ((config->redundancy < 1) ||
            (config->redundancy * 2 >= config->bricks) ||
            !ec_is_power_of_2(config->gf_word_size) ||
            ((config->chunk_size * 8) % (config->gf_word_size * data_bricks) !=
             0)) {
            gf_msg(xl->name, GF_LOG_ERROR, EINVAL, EC_MSG_INVALID_CONFIG,
                   "Invalid or corrupted config");
        } else {
            gf_msg(xl->name, GF_LOG_ERROR, EINVAL, EC_MSG_INVALID_CONFIG,
                   "Unsupported config "
                   "(V=%u, A=%u, W=%u, "
                   "N=%u, R=%u, S=%u)",
                   config->version, config->algorithm, config->gf_word_size,
                   config->bricks, config->redundancy, config->chunk_size);
        }

        return _gf_false;
    }

    return _gf_true;
}

gf_boolean_t
ec_set_dirty_flag(ec_lock_link_t *link, ec_inode_t *ctx, uint64_t *dirty)
{
    gf_boolean_t set_dirty = _gf_false;

    if (link->update[EC_DATA_TXN] && !ctx->dirty[EC_DATA_TXN]) {
        if (!link->optimistic_changelog)
            dirty[EC_DATA_TXN] = 1;
    }

    if (link->update[EC_METADATA_TXN] && !ctx->dirty[EC_METADATA_TXN]) {
        if (!link->optimistic_changelog)
            dirty[EC_METADATA_TXN] = 1;
    }

    if (dirty[EC_METADATA_TXN] || dirty[EC_DATA_TXN]) {
        set_dirty = _gf_true;
    }

    return set_dirty;
}

int32_t
ec_prepare_update_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
    struct list_head list;
    ec_fop_data_t *fop = cookie, *parent, *tmp;
    ec_lock_link_t *parent_link = fop->data;
    ec_lock_link_t *link = NULL;
    ec_lock_t *lock = NULL;
    ec_inode_t *ctx;
    gf_boolean_t release = _gf_false;
    uint64_t provided_flags = 0;
    uint64_t dirty[EC_VERSION_SIZE] = {0, 0};
    lock = parent_link->lock;
    parent = parent_link->fop;
    ctx = lock->ctx;

    INIT_LIST_HEAD(&list);
    provided_flags = EC_PROVIDED_FLAGS(parent_link->waiting_flags);

    LOCK(&lock->loc.inode->lock);

    list_for_each_entry(link, &lock->owners, owner_list)
    {
        if ((link->waiting_flags & provided_flags) != 0) {
            link->waiting_flags ^= (link->waiting_flags & provided_flags);
            if (EC_NEEDED_FLAGS(link->waiting_flags) == 0)
                list_add_tail(&link->fop->cbk_list, &list);
        }
    }
    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, EC_MSG_SIZE_VERS_GET_FAIL,
               "Failed to get size and version :  %s", ec_msg_str(fop));

        goto unlock;
    }

    if (EC_FLAGS_HAVE(provided_flags, EC_FLAG_XATTROP)) {
        op_errno = -ec_dict_del_array(dict, EC_XATTR_VERSION, ctx->pre_version,
                                      EC_VERSION_SIZE);
        if (op_errno != 0) {
            gf_msg(this->name, GF_LOG_ERROR, op_errno,
                   EC_MSG_VER_XATTR_GET_FAIL, "Unable to get version xattr. %s",
                   ec_msg_str(fop));
            goto unlock;
        }
        ctx->post_version[0] += ctx->pre_version[0];
        ctx->post_version[1] += ctx->pre_version[1];

        ctx->have_version = _gf_true;

        if (lock->loc.inode->ia_type == IA_IFREG ||
            lock->loc.inode->ia_type == IA_INVAL) {
            op_errno = -ec_dict_del_number(dict, EC_XATTR_SIZE, &ctx->pre_size);
            if (op_errno != 0) {
                if (lock->loc.inode->ia_type == IA_IFREG) {
                    gf_msg(this->name, GF_LOG_ERROR, op_errno,
                           EC_MSG_SIZE_XATTR_GET_FAIL,
                           "Unable to get size xattr. %s", ec_msg_str(fop));
                    goto unlock;
                }
            } else {
                ctx->post_size = ctx->pre_size;

                ctx->have_size = _gf_true;
            }

            op_errno = -ec_dict_del_config(dict, EC_XATTR_CONFIG, &ctx->config);
            if (op_errno != 0) {
                if ((lock->loc.inode->ia_type == IA_IFREG) ||
                    (op_errno != ENODATA)) {
                    gf_msg(this->name, GF_LOG_ERROR, op_errno,
                           EC_MSG_CONFIG_XATTR_GET_FAIL,
                           "Unable to get config xattr. %s", ec_msg_str(fop));

                    goto unlock;
                }
            } else {
                if (!ec_config_check(parent->xl, &ctx->config)) {
                    gf_msg(this->name, GF_LOG_ERROR, EINVAL,
                           EC_MSG_CONFIG_XATTR_INVALID, "Invalid config xattr");

                    op_errno = EINVAL;

                    goto unlock;
                }
                ctx->have_config = _gf_true;
            }
        }
        ctx->have_info = _gf_true;
    }

    ec_set_dirty_flag(fop->data, ctx, dirty);
    if (dirty[EC_METADATA_TXN] &&
        (EC_FLAGS_HAVE(provided_flags, EC_FLAG_METADATA_DIRTY))) {
        GF_ASSERT(!ctx->dirty[EC_METADATA_TXN]);
        ctx->dirty[EC_METADATA_TXN] = 1;
    }

    if (dirty[EC_DATA_TXN] &&
        (EC_FLAGS_HAVE(provided_flags, EC_FLAG_DATA_DIRTY))) {
        GF_ASSERT(!ctx->dirty[EC_DATA_TXN]);
        ctx->dirty[EC_DATA_TXN] = 1;
    }
    op_errno = 0;
unlock:

    lock->waiting_flags ^= provided_flags;

    if (op_errno == 0) {
        /* If the fop fails on any of the good bricks, it is important to mark
         * it dirty and update versions right away if dirty was not set before.
         */
        if (lock->good_mask & ~(fop->good | fop->remaining)) {
            release = _gf_true;
        }

        if (parent_link->update[0] && !parent_link->dirty[0]) {
            lock->release |= release;
        }

        if (parent_link->update[1] && !parent_link->dirty[1]) {
            lock->release |= release;
        }

        /* We don't allow the main fop to be executed on bricks that have not
         * succeeded the initial xattrop. */
        ec_lock_update_good(lock, fop);

        /*As of now only data healing marks bricks as healing*/
        lock->healing |= fop->healing;
    }

    UNLOCK(&lock->loc.inode->lock);

    while (!list_empty(&list)) {
        tmp = list_entry(list.next, ec_fop_data_t, cbk_list);
        list_del_init(&tmp->cbk_list);

        if (op_errno == 0) {
            tmp->mask &= fop->good;

            /*As of now only data healing marks bricks as healing*/
            if (ec_is_data_fop(tmp->id)) {
                tmp->healing |= fop->healing;
            }
        }

        ec_resume(tmp, op_errno);
    }

    return 0;
}

static gf_boolean_t
ec_set_needed_flag(ec_lock_t *lock, ec_lock_link_t *link, uint64_t flag)
{
    uint64_t current;

    link->waiting_flags |= EC_FLAG_NEEDS(flag);

    current = EC_NEEDED_FLAGS(lock->waiting_flags);
    if (!EC_FLAGS_HAVE(current, flag)) {
        lock->waiting_flags |= EC_FLAG_NEEDS(flag);
        link->waiting_flags |= EC_FLAG_PROVIDES(flag);

        return _gf_true;
    }

    return _gf_false;
}

static uint64_t
ec_set_xattrop_flags_and_params(ec_lock_t *lock, ec_lock_link_t *link,
                                uint64_t *dirty)
{
    uint64_t oldflags = 0;
    uint64_t newflags = 0;
    ec_inode_t *ctx = lock->ctx;

    oldflags = EC_NEEDED_FLAGS(lock->waiting_flags);

    if (lock->query && !ctx->have_info) {
        ec_set_needed_flag(lock, link, EC_FLAG_XATTROP);
    }

    if (dirty[EC_DATA_TXN]) {
        if (!ec_set_needed_flag(lock, link, EC_FLAG_DATA_DIRTY)) {
            dirty[EC_DATA_TXN] = 0;
        }
    }

    if (dirty[EC_METADATA_TXN]) {
        if (!ec_set_needed_flag(lock, link, EC_FLAG_METADATA_DIRTY)) {
            dirty[EC_METADATA_TXN] = 0;
        }
    }
    newflags = EC_NEEDED_FLAGS(lock->waiting_flags);

    return oldflags ^ newflags;
}

void
ec_get_size_version(ec_lock_link_t *link)
{
    loc_t loc;
    ec_lock_t *lock;
    ec_inode_t *ctx;
    ec_fop_data_t *fop;
    dict_t *dict = NULL;
    dict_t *xdata = NULL;
    ec_t *ec = NULL;
    int32_t error = 0;
    gf_boolean_t set_dirty = _gf_false;
    uint64_t allzero[EC_VERSION_SIZE] = {0, 0};
    uint64_t dirty[EC_VERSION_SIZE] = {0, 0};
    lock = link->lock;
    ctx = lock->ctx;
    fop = link->fop;
    ec = fop->xl->private;
    uint64_t changed_flags = 0;

    if (ec->optimistic_changelog && !(ec->node_mask & ~link->lock->good_mask) &&
        !ec_is_data_fop(fop->id))
        link->optimistic_changelog = _gf_true;

    memset(&loc, 0, sizeof(loc));

    LOCK(&lock->loc.inode->lock);

    set_dirty = ec_set_dirty_flag(link, ctx, dirty);

    /* If ec metadata has already been retrieved, do not try again. */
    if (ctx->have_info) {
        if (ec_is_data_fop(fop->id)) {
            fop->healing |= lock->healing;
        }
        if (!set_dirty)
            goto unlock;
    }

    /* Determine if there's something we need to retrieve for the current
     * operation. */
    if (!set_dirty && !lock->query && (lock->loc.inode->ia_type != IA_IFREG) &&
        (lock->loc.inode->ia_type != IA_INVAL)) {
        goto unlock;
    }

    changed_flags = ec_set_xattrop_flags_and_params(lock, link, dirty);
    if (link->waiting_flags) {
        /* This fop needs to wait until all its flags are cleared which
         * potentially can be cleared by other xattrops that are already
         * wound*/
        ec_sleep(fop);
    } else {
        GF_ASSERT(!changed_flags);
    }

unlock:
    UNLOCK(&lock->loc.inode->lock);

    if (!changed_flags)
        goto out;

    dict = dict_new();
    if (dict == NULL) {
        error = -ENOMEM;
        goto out;
    }

    if (EC_FLAGS_HAVE(changed_flags, EC_FLAG_XATTROP)) {
        /* Once we know that an xattrop will be needed,
         * we try to get all available information in a
         * single call. */
        error = ec_dict_set_array(dict, EC_XATTR_VERSION, allzero,
                                  EC_VERSION_SIZE);
        if (error != 0) {
            goto out;
        }

        if (lock->loc.inode->ia_type == IA_IFREG ||
            lock->loc.inode->ia_type == IA_INVAL) {
            error = ec_dict_set_number(dict, EC_XATTR_SIZE, 0);
            if (error == 0) {
                error = ec_dict_set_number(dict, EC_XATTR_CONFIG, 0);
            }
            if (error != 0) {
                goto out;
            }

            xdata = dict_new();
            if (xdata == NULL || dict_set_int32(xdata, GF_GET_SIZE, 1)) {
                error = -ENOMEM;
                goto out;
            }
        }
    }

    if (memcmp(allzero, dirty, sizeof(allzero))) {
        error = ec_dict_set_array(dict, EC_XATTR_DIRTY, dirty, EC_VERSION_SIZE);
        if (error != 0) {
            goto out;
        }
    }

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    /* For normal fops, ec_[f]xattrop() must succeed on at least
     * EC_MINIMUM_MIN bricks, however when this is called as part of a
     * self-heal operation the mask of target bricks (fop->mask) could
     * contain less than EC_MINIMUM_MIN bricks, causing the xattrop to
     * always fail. Thus we always use the same minimum used for the main
     * fop.
     */
    if (lock->fd == NULL) {
        error = ec_loc_from_loc(fop->xl, &loc, &lock->loc);
        if (error != 0) {
            goto out;
        }
        if (gf_uuid_is_null(loc.pargfid)) {
            if (loc.parent != NULL) {
                inode_unref(loc.parent);
                loc.parent = NULL;
            }
            GF_FREE((char *)loc.path);
            loc.path = NULL;
            loc.name = NULL;
        }

        ec_xattrop(fop->frame, fop->xl, fop->mask, fop->minimum,
                   ec_prepare_update_cbk, link, &loc, GF_XATTROP_ADD_ARRAY64,
                   dict, xdata);
    } else {
        ec_fxattrop(fop->frame, fop->xl, fop->mask, fop->minimum,
                    ec_prepare_update_cbk, link, lock->fd,
                    GF_XATTROP_ADD_ARRAY64, dict, xdata);
    }

    error = 0;

out:
    fop->frame->root->uid = fop->uid;
    fop->frame->root->gid = fop->gid;

    loc_wipe(&loc);

    if (dict != NULL) {
        dict_unref(dict);
    }

    if (xdata != NULL) {
        dict_unref(xdata);
    }

    if (error != 0) {
        ec_fop_set_error(fop, -error);
    }
}

gf_boolean_t
__ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t *size)
{
    ec_inode_t *ctx;
    gf_boolean_t found = _gf_false;

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto out;
    }

    if (ctx->have_size) {
        *size = ctx->post_size;
        found = _gf_true;
    }

out:
    return found;
}

gf_boolean_t
ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t *size)
{
    gf_boolean_t found = _gf_false;

    LOCK(&inode->lock);
    {
        found = __ec_get_inode_size(fop, inode, size);
    }
    UNLOCK(&inode->lock);

    return found;
}

gf_boolean_t
__ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t size)
{
    ec_inode_t *ctx;
    gf_boolean_t found = _gf_false;

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto out;
    }

    /* Normal fops always have ctx->have_size set. However self-heal calls this
     * to prepare the inode, so ctx->have_size will be false. In this case we
     * prepare both pre_size and post_size, and set have_size and have_info to
     * true. */
    if (!ctx->have_size) {
        ctx->pre_size = size;
        ctx->have_size = ctx->have_info = _gf_true;
    }
    ctx->post_size = size;

    found = _gf_true;

out:
    return found;
}

gf_boolean_t
ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t size)
{
    gf_boolean_t found = _gf_false;

    LOCK(&inode->lock);
    {
        found = __ec_set_inode_size(fop, inode, size);
    }
    UNLOCK(&inode->lock);

    return found;
}

static void
ec_release_stripe_cache(ec_inode_t *ctx)
{
    ec_stripe_list_t *stripe_cache = NULL;
    ec_stripe_t *stripe = NULL;

    stripe_cache = &ctx->stripe_cache;
    while (!list_empty(&stripe_cache->lru)) {
        stripe = list_first_entry(&stripe_cache->lru, ec_stripe_t, lru);
        list_del(&stripe->lru);
        GF_FREE(stripe);
    }
    stripe_cache->count = 0;
    stripe_cache->max = 0;
}

void
ec_clear_inode_info(ec_fop_data_t *fop, inode_t *inode)
{
    ec_inode_t *ctx;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto unlock;
    }

    ec_release_stripe_cache(ctx);
    ctx->have_info = _gf_false;
    ctx->have_config = _gf_false;
    ctx->have_version = _gf_false;
    ctx->have_size = _gf_false;

    memset(&ctx->config, 0, sizeof(ctx->config));
    memset(ctx->pre_version, 0, sizeof(ctx->pre_version));
    memset(ctx->post_version, 0, sizeof(ctx->post_version));
    ctx->pre_size = ctx->post_size = 0;
    memset(ctx->dirty, 0, sizeof(ctx->dirty));

unlock:
    UNLOCK(&inode->lock);
}

int32_t
ec_get_real_size_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link;

    if (op_ret >= 0) {
        link = fop->data;
        link->size = buf->ia_size;
    } else {
        /* Prevent failure of parent fop. */
        fop->error = 0;
    }

    return 0;
}

/* This function is used to get the trusted.ec.size xattr from a file when
 * no lock is needed on the inode. This is only required to maintain iatt
 * structs on fops that manipulate directory entries but do not operate
 * directly on the inode, like link, rename, ...
 *
 * Any error processing this request is ignored. In the worst case, an invalid
 * or not up to date value in the iatt could cause some cache invalidation.
 */
void
ec_get_real_size(ec_lock_link_t *link)
{
    ec_fop_data_t *fop;
    dict_t *xdata;

    if (link->base == NULL || link->base->inode == NULL) {
        return;
    }

    if (link->base->inode->ia_type != IA_IFREG) {
        return;
    }

    fop = link->fop;

    if (ec_get_inode_size(fop, link->base->inode, &link->size)) {
        return;
    }

    xdata = dict_new();
    if (xdata == NULL) {
        return;
    }
    if (ec_dict_set_number(xdata, EC_XATTR_SIZE, 0) != 0) {
        goto out;
    }

    /* Send a simple lookup. A single answer is considered ok since this value
     * is only used to return an iatt struct related to an inode that is not
     * locked and have not suffered any operation. */
    ec_lookup(fop->frame, fop->xl, fop->mask, 1, ec_get_real_size_cbk, link,
              link->base, xdata);

out:
    if (xdata != NULL) {
        dict_unref(xdata);
    }
}

static void
ec_lock_update_fd(ec_lock_t *lock, ec_fop_data_t *fop)
{
    /* If the fop has an fd available, attach it to the lock structure to be
     * able to do fxattrop calls instead of xattrop. */
    if (fop->use_fd && (lock->fd == NULL)) {
        lock->fd = __fd_ref(fop->fd);
    }
}

static gf_boolean_t
ec_link_has_lock_conflict(ec_lock_link_t *link, gf_boolean_t waitlist_check)
{
    ec_lock_link_t *trav_link = NULL;

    list_for_each_entry(trav_link, &link->lock->owners, owner_list)
    {
        if (ec_lock_conflict(trav_link, link))
            return _gf_true;
    }

    if (!waitlist_check)
        return _gf_false;

    list_for_each_entry(trav_link, &link->lock->waiting, wait_list)
    {
        if (ec_lock_conflict(trav_link, link))
            return _gf_true;
    }

    return _gf_false;
}

static void
ec_lock_wake_shared(ec_lock_t *lock, struct list_head *list)
{
    ec_fop_data_t *fop;
    ec_lock_link_t *link;
    gf_boolean_t conflict = _gf_false;

    while (!conflict && !list_empty(&lock->waiting)) {
        link = list_entry(lock->waiting.next, ec_lock_link_t, wait_list);
        fop = link->fop;

        /* If lock is not acquired, at most one fop can be assigned as owner.
         * The following fops will need to wait in the lock->waiting queue
         * until the lock has been fully acquired. */
        conflict = !lock->acquired;

        /* If the fop is not shareable, only this fop can be assigned as owner.
         * Other fops will need to wait until this one finishes. */
        if (ec_link_has_lock_conflict(link, _gf_false)) {
            conflict = _gf_true;
        }

        /* If only one fop is allowed, it can be assigned as the owner of the
         * lock only if there weren't any other owner. */
        if (conflict && !list_empty(&lock->owners)) {
            break;
        }

        list_move_tail(&link->wait_list, list);

        list_add_tail(&link->owner_list, &lock->owners);
        lock->refs_owners++;

        ec_lock_update_fd(lock, fop);
    }
}

static void
ec_lock_apply(ec_lock_link_t *link)
{
    ec_fop_data_t *fop = link->fop;

    fop->mask &= link->lock->good_mask;
    fop->locked++;

    ec_get_size_version(link);
    ec_get_real_size(link);
}

gf_boolean_t
ec_lock_acquire(ec_lock_link_t *link);

static void
ec_lock_resume_shared(struct list_head *list)
{
    ec_lock_link_t *link;

    while (!list_empty(list)) {
        link = list_entry(list->next, ec_lock_link_t, wait_list);
        list_del_init(&link->wait_list);

        if (link->lock->acquired) {
            ec_lock_apply(link);
            ec_lock(link->fop);
        } else {
            GF_ASSERT(list_empty(list));

            ec_lock_acquire(link);
        }

        ec_resume(link->fop, 0);
    }
}

void
ec_lock_acquired(ec_lock_link_t *link)
{
    struct list_head list;
    ec_lock_t *lock;
    ec_fop_data_t *fop;

    lock = link->lock;
    fop = link->fop;

    ec_trace("LOCKED", fop, "lock=%p", lock);

    INIT_LIST_HEAD(&list);

    LOCK(&lock->loc.inode->lock);

    lock->acquired = _gf_true;
    if (lock->contention) {
        lock->release = _gf_true;
        lock->contention = _gf_false;
    }

    ec_lock_update_fd(lock, fop);
    ec_lock_wake_shared(lock, &list);

    UNLOCK(&lock->loc.inode->lock);

    ec_lock_apply(link);

    if (fop->use_fd &&
        (link->update[EC_DATA_TXN] || link->update[EC_METADATA_TXN])) {
        /* Try to reopen closed fd's only if lock has succeeded. */
        ec_fix_open(fop, lock->mask);
    }

    ec_lock_resume_shared(&list);
}

int32_t
ec_locked(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
          int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link = NULL;
    ec_lock_t *lock = NULL;

    link = fop->data;
    lock = link->lock;
    if (op_ret >= 0) {
        lock->mask = lock->good_mask = fop->good;
        lock->healing = 0;

        ec_lock_acquired(link);
        ec_lock(fop->parent);
    } else {
        LOCK(&lock->loc.inode->lock);
        {
            lock->contention = _gf_false;
        }
        UNLOCK(&lock->loc.inode->lock);
        gf_msg(this->name, GF_LOG_WARNING, op_errno, EC_MSG_PREOP_LOCK_FAILED,
               "Failed to complete preop lock");
    }

    return 0;
}

gf_boolean_t
ec_lock_acquire(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_fop_data_t *fop;
    gf_lkowner_t lk_owner;

    lock = link->lock;
    fop = link->fop;

    if (!lock->acquired) {
        set_lk_owner_from_ptr(&lk_owner, lock);

        ec_trace("LOCK_ACQUIRE", fop, "lock=%p, inode=%p", lock,
                 lock->loc.inode);

        lock->flock.l_type = F_WRLCK;
        ec_inodelk(fop->frame, fop->xl, &lk_owner, -1, EC_MINIMUM_ALL,
                   ec_locked, link, fop->xl->name, &lock->loc, F_SETLKW,
                   &lock->flock, NULL);

        return _gf_false;
    }

    ec_trace("LOCK_REUSE", fop, "lock=%p", lock);

    ec_lock_acquired(link);

    return _gf_true;
}

static ec_lock_link_t *
ec_lock_timer_cancel(xlator_t *xl, ec_lock_t *lock)
{
    ec_lock_link_t *timer_link;

    /* If we don't have any timer, there's nothing to cancel. */
    if (lock->timer == NULL) {
        return NULL;
    }

    /* We are trying to access a lock that has an unlock timer active.
     * This means that the lock must be idle, i.e. no fop can be in the
     * owner, waiting or frozen lists. It also means that the lock cannot
     * have been marked as being released (this is done without timers).
     * There should only be one owner reference, but it's possible that
     * some fops are being prepared to use this lock. */
    GF_ASSERT((lock->refs_owners == 1) && list_empty(&lock->owners) &&
              list_empty(&lock->waiting));

    /* We take the timer_link before cancelling the timer, since a
     * successful cancellation will destroy it. It must not be NULL
     * because it references the fop responsible for the delayed unlock
     * that we are currently trying to cancel. */
    timer_link = lock->timer->data;
    GF_ASSERT(timer_link != NULL);

    if (gf_timer_call_cancel(xl->ctx, lock->timer) < 0) {
        /* It's too late to avoid the execution of the timer callback.
         * Since we need to be sure that the callback has access to all
         * needed resources, we cannot resume the execution of the
         * timer fop now. This will be done in the callback. */
        timer_link = NULL;
    } else {
        /* The timer has been cancelled. The fop referenced by
         * timer_link holds the last reference. The caller is
         * responsible to release it when not needed anymore. */
        ec_trace("UNLOCK_CANCELLED", timer_link->fop, "lock=%p", lock);
    }

    /* We have two options here:
     *
     * 1. The timer has been successfully cancelled.
     *
     *    This is the easiest case and we can continue with the currently
     *    acquired lock.
     *
     * 2. The timer callback has already been fired.
     *
     *    In this case we have not been able to cancel the timer before
     *    the timer callback has been fired, but we also know that
     *    lock->timer != NULL. This means that the timer callback is still
     *    trying to acquire the inode mutex that we currently own. We are
     *    safe until we release it. In this case we can safely clear
     *    lock->timer. This will cause that the timer callback does nothing
     *    once it acquires the mutex.
     */
    lock->timer = NULL;

    return timer_link;
}

static gf_boolean_t
ec_lock_assign_owner(ec_lock_link_t *link)
{
    ec_fop_data_t *fop;
    ec_lock_t *lock;
    ec_lock_link_t *timer_link = NULL;
    gf_boolean_t assigned = _gf_false;

    /* The link cannot be in any list because we have just finished preparing
     * it. */
    GF_ASSERT(list_empty(&link->wait_list));

    fop = link->fop;
    lock = link->lock;

    LOCK(&lock->loc.inode->lock);

    /* Since the link has just been prepared but it's not active yet, the
     * refs_pending must be one at least (the ref owned by this link). */
    GF_ASSERT(lock->refs_pending > 0);
    /* The link is not pending any more. It will be assigned to the owner,
     * waiting or frozen list. */
    lock->refs_pending--;

    if (lock->release) {
        ec_trace("LOCK_QUEUE_FREEZE", fop, "lock=%p", lock);

        /* When lock->release is set, we'll unlock the lock as soon as
         * possible, meaning that we won't use a timer. */
        GF_ASSERT(lock->timer == NULL);

        /* The lock is marked to be released. We can still have owners and fops
         * in the waiting ilist f they have been added before the lock has been
         * marked to be released. However new fops are put into the frozen list
         * to wait for the next unlock/lock cycle. */
        list_add_tail(&link->wait_list, &lock->frozen);

        goto unlock;
    }

    /* The lock is not marked to be released, so the frozen list should be
     * empty. */
    GF_ASSERT(list_empty(&lock->frozen));

    timer_link = ec_lock_timer_cancel(fop->xl, lock);

    if (!list_empty(&lock->owners)) {
        /* There are other owners of this lock. We can only take ownership if
         * the lock is already acquired and doesn't have conflict with existing
         * owners, or waiters(to prevent starvation).
         * Otherwise we need to wait.
         */
        if (!lock->acquired || ec_link_has_lock_conflict(link, _gf_true)) {
            ec_trace("LOCK_QUEUE_WAIT", fop, "lock=%p", lock);

            list_add_tail(&link->wait_list, &lock->waiting);

            goto unlock;
        }
    }

    list_add_tail(&link->owner_list, &lock->owners);

    /* If timer_link is not NULL, it means that we have inherited the owner
     * reference assigned to the timer fop. In this case we simply reuse it.
     * Otherwise we need to increase the number of owners. */
    if (timer_link == NULL) {
        lock->refs_owners++;
    }

    assigned = _gf_true;

unlock:
    if (!assigned) {
        /* We have not been able to take ownership of this lock. The fop must
         * be put to sleep. */
        ec_sleep(fop);
    }

    UNLOCK(&lock->loc.inode->lock);

    /* If we have cancelled the timer, we need to resume the fop that was
     * waiting for it. */
    if (timer_link != NULL) {
        ec_resume(timer_link->fop, 0);
    }

    return assigned;
}

static void
ec_lock_next_owner(ec_lock_link_t *link, ec_cbk_data_t *cbk,
                   gf_boolean_t release)
{
    struct list_head list;
    ec_lock_t *lock = link->lock;
    ec_fop_data_t *fop = link->fop;
    ec_inode_t *ctx = lock->ctx;

    INIT_LIST_HEAD(&list);

    LOCK(&lock->loc.inode->lock);

    ec_trace("LOCK_DONE", fop, "lock=%p", lock);

    /* Current link must belong to the owner list of the lock. We don't
     * decrement lock->refs_owners here because the inode mutex is released
     * before ec_unlock() is called and we need to know when the last owner
     * unlocks the lock to do proper cleanup. lock->refs_owners is used for
     * this task. */
    GF_ASSERT((lock->refs_owners > 0) && !list_empty(&link->owner_list));
    list_del_init(&link->owner_list);

    lock->release |= release;

    if ((fop->error == 0) && (cbk != NULL) && (cbk->op_ret >= 0)) {
        if (link->update[0]) {
            ctx->post_version[0]++;
        }
        if (link->update[1]) {
            ctx->post_version[1]++;
        }
        /* If the fop fails on any of the good bricks, it is important to mark
         * it dirty and update versions right away. */
        if (link->update[0] || link->update[1]) {
            if (lock->good_mask & ~(fop->good | fop->remaining)) {
                lock->release = _gf_true;
            }
        }
    }

    if (fop->healing) {
        lock->healing = fop->healing & (fop->good | fop->remaining);
    }
    ec_lock_update_good(lock, fop);

    ec_lock_wake_shared(lock, &list);

    UNLOCK(&lock->loc.inode->lock);

    ec_lock_resume_shared(&list);
}

void
ec_lock(ec_fop_data_t *fop)
{
    ec_lock_link_t *link;

    /* There is a chance that ec_resume is called on fop even before ec_sleep.
     * Which can result in refs == 0 for fop leading to use after free in this
     * function when it calls ec_sleep so do ec_sleep at start and ec_resume at
     * the end of this function.*/
    ec_sleep(fop);

    while (fop->locked < fop->lock_count) {
        /* Since there are only up to 2 locks per fop, this xor will change
         * the order of the locks if fop->first_lock is 1. */
        link = &fop->locks[fop->locked ^ fop->first_lock];

        if (!ec_lock_assign_owner(link) || !ec_lock_acquire(link)) {
            break;
        }
    }

    ec_resume(fop, 0);
}

void
ec_lock_unfreeze(ec_lock_link_t *link)
{
    struct list_head list;
    ec_lock_t *lock;
    gf_boolean_t destroy = _gf_false;

    lock = link->lock;

    INIT_LIST_HEAD(&list);

    LOCK(&lock->loc.inode->lock);

    /* The lock must be marked to be released here, since we have just released
     * it and any attempt to assign it to more fops must have added them to the
     * frozen list. We can only have one active reference here: the one that
     * is processing this unfreeze. */
    GF_ASSERT(lock->release && (lock->refs_owners == 1));
    lock->release = _gf_false;
    lock->refs_owners = 0;

    lock->acquired = _gf_false;

    /* We are unfreezing a lock. This means that the lock has already been
     * released. In this state it shouldn't have a pending timer nor have any
     * owner, and the waiting list should be empty. Only the frozen list can
     * contain some fop. */
    GF_ASSERT((lock->timer == NULL) && list_empty(&lock->waiting) &&
              list_empty(&lock->owners));

    /* We move all frozen fops to the waiting list. */
    list_splice_init(&lock->frozen, &lock->waiting);

    /* If we don't have any fop waiting nor there are any prepared fops using
     * this lock, we can finally dispose it. */
    destroy = list_empty(&lock->waiting) && (lock->refs_pending == 0);
    if (destroy) {
        ec_trace("LOCK_DESTROY", link->fop, "lock=%p", lock);

        lock->ctx->inode_lock = NULL;
    } else {
        ec_trace("LOCK_UNFREEZE", link->fop, "lock=%p", lock);

        ec_lock_wake_shared(lock, &list);
    }

    UNLOCK(&lock->loc.inode->lock);

    ec_lock_resume_shared(&list);

    if (destroy) {
        ec_lock_destroy(lock);
    }
}

int32_t
ec_unlocked(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link = fop->data;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, EC_MSG_UNLOCK_FAILED,
               "entry/inode unlocking failed :(%s)", ec_msg_str(link->fop));
    } else {
        ec_trace("UNLOCKED", link->fop, "lock=%p", link->lock);
    }

    ec_lock_unfreeze(link);

    return 0;
}

void
ec_unlock_lock(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_fop_data_t *fop;
    gf_lkowner_t lk_owner;

    lock = link->lock;
    fop = link->fop;

    lock->unlock_now = _gf_false;
    ec_clear_inode_info(fop, lock->loc.inode);

    if ((lock->mask != 0) && lock->acquired) {
        set_lk_owner_from_ptr(&lk_owner, lock);
        lock->flock.l_type = F_UNLCK;
        ec_trace("UNLOCK_INODELK", fop, "lock=%p, inode=%p", lock,
                 lock->loc.inode);

        ec_inodelk(fop->frame, fop->xl, &lk_owner, lock->mask, EC_MINIMUM_ONE,
                   ec_unlocked, link, fop->xl->name, &lock->loc, F_SETLK,
                   &lock->flock, NULL);
    } else {
        ec_lock_unfreeze(link);
    }
}

void
ec_inode_bad_inc(inode_t *inode, xlator_t *xl)
{
    ec_inode_t *ctx = NULL;

    LOCK(&inode->lock);
    {
        ctx = __ec_inode_get(inode, xl);
        if (ctx == NULL) {
            goto unlock;
        }
        ctx->bad_version++;
    }
unlock:
    UNLOCK(&inode->lock);
}

int32_t
ec_update_size_version_done(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *xattr,
                            dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link;
    ec_lock_t *lock;
    ec_inode_t *ctx;

    link = fop->data;
    lock = link->lock;
    ctx = lock->ctx;

    if (op_ret < 0) {
        if (link->lock->fd == NULL) {
            ec_inode_bad_inc(link->lock->loc.inode, this);
        } else {
            ec_inode_bad_inc(link->lock->fd->inode, this);
        }

        gf_msg(fop->xl->name, fop_log_level(fop->id, op_errno), op_errno,
               EC_MSG_SIZE_VERS_UPDATE_FAIL,
               "Failed to update version and size. %s", ec_msg_str(fop));
    } else {
        fop->parent->good &= fop->good;

        ec_lock_update_good(lock, fop);

        if (ec_dict_del_array(xattr, EC_XATTR_VERSION, ctx->post_version,
                              EC_VERSION_SIZE) == 0) {
            ctx->pre_version[0] = ctx->post_version[0];
            ctx->pre_version[1] = ctx->post_version[1];

            ctx->have_version = _gf_true;
        }
        if (ec_dict_del_number(xattr, EC_XATTR_SIZE, &ctx->post_size) == 0) {
            ctx->pre_size = ctx->post_size;

            ctx->have_size = _gf_true;
        }
        if ((ec_dict_del_config(xdata, EC_XATTR_CONFIG, &ctx->config) == 0) &&
            ec_config_check(fop->xl, &ctx->config)) {
            ctx->have_config = _gf_true;
        }

        ctx->have_info = _gf_true;
    }
    /* If we are here because of fop's and other than unlock request,
     * that means we are still holding a lock. That make sure
     * lock->unlock_now can not be modified.
     */
    if (lock->unlock_now) {
        ec_unlock_lock(fop->data);
    }

    return 0;
}

void
ec_update_size_version(ec_lock_link_t *link, uint64_t *version, uint64_t size,
                       uint64_t *dirty)
{
    ec_fop_data_t *fop;
    ec_lock_t *lock;
    ec_inode_t *ctx;
    dict_t *dict = NULL;
    uintptr_t update_on = 0;
    int32_t err = -ENOMEM;

    fop = link->fop;
    lock = link->lock;
    ctx = lock->ctx;

    ec_trace("UPDATE", fop, "version=%ld/%ld, size=%ld, dirty=%ld/%ld",
             version[0], version[1], size, dirty[0], dirty[1]);

    dict = dict_new();
    if (dict == NULL) {
        goto out;
    }

    /* If we don't have version information or it has been modified, we
     * update it. */
    if (!ctx->have_version || (version[0] != 0) || (version[1] != 0)) {
        err = ec_dict_set_array(dict, EC_XATTR_VERSION, version,
                                EC_VERSION_SIZE);
        if (err != 0) {
            goto out;
        }
    }

    if (size != 0) {
        /* If size has been changed, we should already
         * know the previous size of the file. */
        GF_ASSERT(ctx->have_size);

        err = ec_dict_set_number(dict, EC_XATTR_SIZE, size);
        if (err != 0) {
            goto out;
        }
    }

    if (dirty[0] || dirty[1]) {
        err = ec_dict_set_array(dict, EC_XATTR_DIRTY, dirty, EC_VERSION_SIZE);
        if (err != 0) {
            goto out;
        }
    }

    /* If config information is not known, we request it now. */
    if ((lock->loc.inode->ia_type == IA_IFREG) && !ctx->have_config) {
        /* A failure requesting this xattr is ignored because it's not
         * absolutely required right now. */
        (void)ec_dict_set_number(dict, EC_XATTR_CONFIG, 0);
    }

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    update_on = lock->good_mask | lock->healing;

    if (link->lock->fd == NULL) {
        ec_xattrop(fop->frame, fop->xl, update_on, EC_MINIMUM_MIN,
                   ec_update_size_version_done, link, &link->lock->loc,
                   GF_XATTROP_ADD_ARRAY64, dict, NULL);
    } else {
        ec_fxattrop(fop->frame, fop->xl, update_on, EC_MINIMUM_MIN,
                    ec_update_size_version_done, link, link->lock->fd,
                    GF_XATTROP_ADD_ARRAY64, dict, NULL);
    }

    fop->frame->root->uid = fop->uid;
    fop->frame->root->gid = fop->gid;

    dict_unref(dict);

    return;

out:
    if (dict != NULL) {
        dict_unref(dict);
    }

    ec_fop_set_error(fop, -err);

    gf_msg(fop->xl->name, GF_LOG_ERROR, -err, EC_MSG_SIZE_VERS_UPDATE_FAIL,
           "Unable to update version and size. %s", ec_msg_str(fop));

    if (lock->unlock_now) {
        ec_unlock_lock(fop->data);
    }
}

gf_boolean_t
ec_update_info(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_inode_t *ctx;
    uint64_t version[2] = {0, 0};
    uint64_t dirty[2] = {0, 0};
    uint64_t size;
    ec_t *ec = NULL;
    uintptr_t mask;

    lock = link->lock;
    ctx = lock->ctx;
    ec = link->fop->xl->private;

    /* pre_version[*] will be 0 if have_version is false */
    version[EC_DATA_TXN] = ctx->post_version[EC_DATA_TXN] -
                           ctx->pre_version[EC_DATA_TXN];
    version[EC_METADATA_TXN] = ctx->post_version[EC_METADATA_TXN] -
                               ctx->pre_version[EC_METADATA_TXN];

    size = ctx->post_size - ctx->pre_size;
    /* If we set the dirty flag for update fop, we have to unset it.
     * If fop has failed on some bricks, leave the dirty as marked. */

    if (lock->unlock_now) {
        if (version[EC_DATA_TXN]) {
            /*A data fop will have difference in post and pre version
             *and for data fop we send writes on healing bricks also */
            mask = lock->good_mask | lock->healing;
        } else {
            mask = lock->good_mask;
        }
        /* Ensure that nodes are up while doing final
         * metadata update.*/
        if (!(ec->node_mask & ~(mask)) && !(ec->node_mask & ~ec->xl_up)) {
            if (ctx->dirty[EC_DATA_TXN] != 0) {
                dirty[EC_DATA_TXN] = -1;
            }
            if (ctx->dirty[EC_METADATA_TXN] != 0) {
                dirty[EC_METADATA_TXN] = -1;
            }
            /*If everything is fine and we already
             *have version xattr set on entry, there
             *is no need to update version again*/
            if (ctx->pre_version[EC_DATA_TXN]) {
                version[EC_DATA_TXN] = 0;
            }
            if (ctx->pre_version[EC_METADATA_TXN]) {
                version[EC_METADATA_TXN] = 0;
            }
        } else {
            link->optimistic_changelog = _gf_false;
            ec_set_dirty_flag(link, ctx, dirty);
        }
        memset(ctx->dirty, 0, sizeof(ctx->dirty));
    }

    if ((version[EC_DATA_TXN] != 0) || (version[EC_METADATA_TXN] != 0) ||
        (dirty[EC_DATA_TXN] != 0) || (dirty[EC_METADATA_TXN] != 0)) {
        ec_update_size_version(link, version, size, dirty);
        return _gf_true;
    }

    return _gf_false;
}

void
ec_unlock_now(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    lock = link->lock;

    ec_trace("UNLOCK_NOW", link->fop, "lock=%p", link->lock);
    /*At this point, lock is not being used by any fop and
     *can not be reused by any fop as it is going to be released.
     *lock->unlock_now can not be modified at any other place.
     */
    lock->unlock_now = _gf_true;

    if (!ec_update_info(link)) {
        ec_unlock_lock(link);
    }

    ec_resume(link->fop, 0);
}

void
ec_lock_release(ec_t *ec, inode_t *inode)
{
    ec_lock_t *lock;
    ec_inode_t *ctx;
    ec_lock_link_t *timer_link = NULL;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, ec->xl);
    if (ctx == NULL) {
        goto done;
    }
    lock = ctx->inode_lock;
    if ((lock == NULL) || lock->release) {
        goto done;
    }

    gf_msg_debug(ec->xl->name, 0, "Releasing inode %p due to lock contention",
                 inode);

    if (!lock->acquired) {
        /* This happens if some bricks already got the lock while inodelk is in
         * progress.  Set release to true after lock is acquired*/
        lock->contention = _gf_true;
        goto done;
    }

    /* The lock is not marked to be released, so the frozen list should be
     * empty. */
    GF_ASSERT(list_empty(&lock->frozen));

    timer_link = ec_lock_timer_cancel(ec->xl, lock);

    /* We mark the lock to be released as soon as possible. */
    lock->release = _gf_true;

done:
    UNLOCK(&inode->lock);

    /* If we have cancelled the timer, we need to start the unlock of the
     * inode. If there was a timer but we have been unable to cancel it
     * because it was just triggered, the timer callback will take care
     * of releasing the inode. */
    if (timer_link != NULL) {
        ec_unlock_now(timer_link);
    }
}

void
ec_unlock_timer_add(ec_lock_link_t *link);

void
ec_unlock_timer_del(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    inode_t *inode;
    gf_boolean_t now = _gf_false;

    /* If we are here, it means that the timer has expired before having
     * been cancelled. This guarantees that 'link' is still valid because
     * the fop that contains it must be pending (if timer cancellation in
     * ec_lock_assign_owner() fails, the fop is left sleeping).
     *
     * At the same time, the fop still has a reference to the lock, so
     * it must also be valid.
     */
    lock = link->lock;

    /* 'lock' must have a valid inode since it can only be destroyed
     * when the lock itself is destroyed, but we have a reference to the
     * lock to avoid this.
     */
    inode = lock->loc.inode;

    LOCK(&inode->lock);

    if (lock->timer != NULL) {
        ec_trace("UNLOCK_DELAYED", link->fop, "lock=%p", lock);

        /* The unlock timer has expired without anyone cancelling it.
         * This means that it shouldn't have any owner, and the waiting
         * and frozen lists should be empty.  It must have only one
         * owner reference, but there can be fops being prepared
         * though.
         * */
        GF_ASSERT(!lock->release && (lock->refs_owners == 1) &&
                  list_empty(&lock->owners) && list_empty(&lock->waiting) &&
                  list_empty(&lock->frozen));

        gf_timer_call_cancel(link->fop->xl->ctx, lock->timer);
        lock->timer = NULL;

        /* Any fop being processed from now on, will need to wait
         * until the next unlock/lock cycle. */
        lock->release = now = _gf_true;
    }

    UNLOCK(&inode->lock);

    if (now) {
        ec_unlock_now(link);
    } else {
        /* The timer has been cancelled just after firing it but before
         * getting here. This means that another fop has used the lock
         * and everything should be handled as if this callback were
         * have not been executed. However we still have an owner
         * reference.
         *
         * We need to release our reference. If this is not the last
         * reference (the most common case because another fop has
         * taken another ref) we only need to decrement the counter.
         * Otherwise we have been delayed enough so that the other fop
         * has had time to acquire the reference, do its operation and
         * release it. At the time of releasing it, the fop did found
         * that the ref counter was > 1 (our reference), so the delayed
         * unlock timer wasn't started. We need to start it again if we
         * are the last reference.
         *
         * ec_unlock_timer_add() handles both cases.
         */
        ec_unlock_timer_add(link);

        /* We need to resume the fop that was waiting for the delayed
         * unlock.
         */
        ec_resume(link->fop, 0);
    }
}

void
ec_unlock_timer_cbk(void *data)
{
    ec_unlock_timer_del(data);
}

static gf_boolean_t
ec_eager_lock_used(ec_t *ec, ec_fop_data_t *fop)
{
    /* Fops with no locks at this point mean that they are sent as sub-fops
     * of other higher level fops. In this case we simply assume that the
     * parent fop will take correct care of the eager lock. */
    if (fop->lock_count == 0) {
        return _gf_true;
    }

    /* We may have more than one lock, but this only happens in the rename
     * fop, and both locks will reference an inode of the same type (a
     * directory in this case), so we only need to check the first lock. */
    if (fop->locks[0].lock->loc.inode->ia_type == IA_IFREG) {
        return ec->eager_lock;
    }

    return ec->other_eager_lock;
}

static uint32_t
ec_eager_lock_timeout(ec_t *ec, ec_lock_t *lock)
{
    if (lock->loc.inode->ia_type == IA_IFREG) {
        return ec->eager_lock_timeout;
    }

    return ec->other_eager_lock_timeout;
}

static gf_boolean_t
ec_lock_delay_create(ec_lock_link_t *link)
{
    struct timespec delay;
    ec_fop_data_t *fop = link->fop;
    ec_lock_t *lock = link->lock;

    delay.tv_sec = ec_eager_lock_timeout(fop->xl->private, lock);
    delay.tv_nsec = 0;
    lock->timer = gf_timer_call_after(fop->xl->ctx, delay, ec_unlock_timer_cbk,
                                      link);
    if (lock->timer == NULL) {
        gf_msg(fop->xl->name, GF_LOG_WARNING, ENOMEM,
               EC_MSG_UNLOCK_DELAY_FAILED, "Unable to delay an unlock");

        return _gf_false;
    }

    return _gf_true;
}

void
ec_unlock_timer_add(ec_lock_link_t *link)
{
    ec_fop_data_t *fop = link->fop;
    ec_lock_t *lock = link->lock;
    gf_boolean_t now = _gf_false;

    LOCK(&lock->loc.inode->lock);

    /* We are trying to unlock the lock. We can have multiple scenarios here,
     * but all of them need to have lock->timer == NULL:
     *
     * 1. There are other owners currently running that can call ec_unlock().
     *
     *    None of them can have started the timer until the last one. But this
     *    call should be the consequence of this lastest one.
     *
     * 2. There are fops in the waiting or frozen lists.
     *
     *    These fops cannot call ec_unlock(). So we should be here.
     *
     * We must reach here with at least one owner reference.
     */
    GF_ASSERT((lock->timer == NULL) && (lock->refs_owners > 0));

    /* If the fop detects that a heal is needed, we mark the lock to be
     * released as soon as possible. */
    lock->release |= ec_fop_needs_heal(fop);

    if (lock->refs_owners > 1) {
        ec_trace("UNLOCK_SKIP", fop, "lock=%p", lock);

        /* If there are other owners we cannot do anything else with the lock.
         * Note that the current fop has already been removed from the owners
         * list in ec_lock_reuse(). */
        lock->refs_owners--;

        UNLOCK(&lock->loc.inode->lock);
    } else if (lock->acquired) {
        /* There are no other owners and the lock is acquired. If there were
         * fops waiting, at least one of them should have been promoted to an
         * owner, so the waiting list should be empty. */
        GF_ASSERT(list_empty(&lock->owners) && list_empty(&lock->waiting));

        ec_t *ec = fop->xl->private;

        /* If everything goes as expected this fop will be put to sleep until
         * the timer callback is executed. */
        ec_sleep(fop);

        /* If the lock needs to be released, or ec is shutting down, do not
         * delay lock release. */
        if (!lock->release && !ec->shutdown) {
            ec_trace("UNLOCK_DELAY", fop, "lock=%p, release=%d", lock,
                     lock->release);

            if (!ec_lock_delay_create(link)) {
                /* We are unable to create a new timer. We immediately release
                 * the lock. */
                lock->release = now = _gf_true;
            }

        } else {
            ec_trace("UNLOCK_FORCE", fop, "lock=%p, release=%d", lock,
                     lock->release);
            lock->release = now = _gf_true;
        }

        UNLOCK(&lock->loc.inode->lock);

        if (now) {
            ec_unlock_now(link);
        }
    } else {
        /* There are no owners and the lock is not acquired. This can only
         * happen if a lock attempt has failed and we get to the unlock step
         * of the fop. As in the previous case, the waiting list must be
         * empty. */
        GF_ASSERT(list_empty(&lock->owners) && list_empty(&lock->waiting));

        /* We need to mark the lock to be released to correctly handle fops
         * that may get in after we release the inode mutex but before
         * ec_lock_unfreeze() is processed. */
        lock->release = _gf_true;

        UNLOCK(&lock->loc.inode->lock);

        ec_lock_unfreeze(link);
    }
}

void
ec_unlock(ec_fop_data_t *fop)
{
    int32_t i;

    for (i = 0; i < fop->lock_count; i++) {
        ec_unlock_timer_add(&fop->locks[i]);
    }
}

void
ec_flush_size_version(ec_fop_data_t *fop)
{
    GF_ASSERT(fop->lock_count == 1);
    ec_update_info(&fop->locks[0]);
}

static void
ec_update_stripe(ec_t *ec, ec_stripe_list_t *stripe_cache, ec_stripe_t *stripe,
                 ec_fop_data_t *fop)
{
    off_t base;

    /* On write fops, we only update existing fragments if the write has
     * succeeded. Otherwise, we remove them from the cache. */
    if ((fop->id == GF_FOP_WRITE) && (fop->answer != NULL) &&
        (fop->answer->op_ret >= 0)) {
        base = stripe->frag_offset - fop->frag_range.first;
        base *= ec->fragments;

        /* We check if the stripe offset falls inside the real region
         * modified by the write fop (a write request is allowed,
         * though uncommon, to write less bytes than requested). The
         * current write fop implementation doesn't allow partial
         * writes of fragments, so if there's no error, we are sure
         * that a full stripe has been completely modified or not
         * touched at all. The value of op_ret may not be a multiple
         * of the stripe size because it depends on the requested
         * size by the user, so we update the stripe if the write has
         * modified at least one byte (meaning ec has written the full
         * stripe). */
        if (base < fop->answer->op_ret + fop->head) {
            memcpy(stripe->data, fop->vector[0].iov_base + base,
                   ec->stripe_size);
            list_move_tail(&stripe->lru, &stripe_cache->lru);

            GF_ATOMIC_INC(ec->stats.stripe_cache.updates);
        }
    } else {
        stripe->frag_offset = -1;
        list_move(&stripe->lru, &stripe_cache->lru);

        GF_ATOMIC_INC(ec->stats.stripe_cache.invals);
    }
}

static void
ec_update_cached_stripes(ec_fop_data_t *fop)
{
    uint64_t first;
    uint64_t last;
    ec_stripe_t *stripe = NULL;
    ec_inode_t *ctx = NULL;
    ec_stripe_list_t *stripe_cache = NULL;
    inode_t *inode = NULL;
    struct list_head *temp;
    struct list_head sentinel;

    first = fop->frag_range.first;
    /* 'last' represents the first stripe not touched by the operation */
    last = fop->frag_range.last;

    /* If there are no modified stripes, we don't need to do anything
     * else. */
    if (last <= first) {
        return;
    }

    if (!fop->use_fd) {
        inode = fop->loc[0].inode;
    } else {
        inode = fop->fd->inode;
    }

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto out;
    }
    stripe_cache = &ctx->stripe_cache;

    /* Since we'll be moving elements of the list to the tail, we might
     * end in an infinite loop. To avoid it, we insert a sentinel element
     * into the list, so that it will be used to detect when we have
     * traversed all existing elements once. */
    list_add_tail(&sentinel, &stripe_cache->lru);
    temp = stripe_cache->lru.next;
    while (temp != &sentinel) {
        stripe = list_entry(temp, ec_stripe_t, lru);
        temp = temp->next;
        if ((first <= stripe->frag_offset) && (stripe->frag_offset < last)) {
            ec_update_stripe(fop->xl->private, stripe_cache, stripe, fop);
        }
    }
    list_del(&sentinel);

out:
    UNLOCK(&inode->lock);
}

void
ec_lock_reuse(ec_fop_data_t *fop)
{
    ec_cbk_data_t *cbk;
    ec_t *ec = NULL;
    int32_t i, count;
    gf_boolean_t release = _gf_false;
    ec = fop->xl->private;
    cbk = fop->answer;

    if (ec_eager_lock_used(ec, fop) && cbk != NULL) {
        if (cbk->xdata != NULL) {
            if ((dict_get_int32(cbk->xdata, GLUSTERFS_INODELK_COUNT, &count) ==
                 0) &&
                (count > 1)) {
                release = _gf_true;
            }
            if (release) {
                gf_msg_debug(fop->xl->name, 0, "Lock contention detected");
            }
        }
    } else {
        /* If eager lock is disabled or if we haven't get
         * an answer with enough quorum, we always release
         * the lock. */
        release = _gf_true;
    }
    ec_update_cached_stripes(fop);

    for (i = 0; i < fop->lock_count; i++) {
        ec_lock_next_owner(&fop->locks[i], cbk, release);
    }
}

void
__ec_manager(ec_fop_data_t *fop, int32_t error)
{
    ec_t *ec = fop->xl->private;

    do {
        ec_trace("MANAGER", fop, "error=%d", error);

        if (!ec_must_wind(fop)) {
            if (ec->xl_up_count < ec->fragments) {
                error = ENOTCONN;
            }
        }

        if (error != 0) {
            fop->error = error;
            fop->state = -fop->state;
        }

        if ((fop->state == EC_STATE_END) || (fop->state == -EC_STATE_END)) {
            ec_fop_data_release(fop);

            break;
        }

        /* At each state, fop must not be used anywhere else and there
         * shouldn't be any pending subfop going on. */
        GF_ASSERT(fop->jobs == 0);

        /* While the manager is running we need to avoid that subfops launched
         * from it could finish and call ec_resume() before the fop->handler
         * has completed. This could lead to the same manager being executed
         * by two threads concurrently. ec_check_complete() will take care of
         * this reference. */
        fop->jobs = 1;

        fop->state = fop->handler(fop, fop->state);
        GF_ASSERT(fop->state >= 0);

        error = ec_check_complete(fop, __ec_manager);
    } while (error >= 0);
}

void
ec_manager(ec_fop_data_t *fop, int32_t error)
{
    GF_ASSERT(fop->jobs == 0);
    GF_ASSERT(fop->winds == 0);
    GF_ASSERT(fop->error == 0);

    if (fop->state == EC_STATE_START) {
        fop->state = EC_STATE_INIT;
    }

    __ec_manager(fop, error);
}

gf_boolean_t
__ec_is_last_fop(ec_t *ec)
{
    if ((list_empty(&ec->pending_fops)) &&
        (GF_ATOMIC_GET(ec->async_fop_count) == 0)) {
        return _gf_true;
    }
    return _gf_false;
}
