/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "byte-order.h"

#include "ec-mem-types.h"
#include "ec-data.h"
#include "ec-helpers.h"
#include "ec-combine.h"
#include "ec-common.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec.h"
#include "ec-messages.h"

int32_t ec_child_valid(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    return (idx < ec->nodes) && (((fop->remaining >> idx) & 1) == 1);
}

int32_t ec_child_next(ec_t * ec, ec_fop_data_t * fop, int32_t idx)
{
    while (!ec_child_valid(ec, fop, idx))
    {
        if (++idx >= ec->nodes)
        {
            idx = 0;
        }
        if (idx == fop->first)
        {
            return -1;
        }
    }

    return idx;
}

uintptr_t ec_inode_good(inode_t * inode, xlator_t * xl)
{
    ec_inode_t * ctx;
    uintptr_t bad = 0;

    ctx = ec_inode_get(inode, xl);
    if (ctx != NULL)
    {
        bad = ctx->bad;
    }

    return ~bad;
}

uintptr_t ec_fd_good(fd_t * fd, xlator_t * xl)
{
    ec_fd_t * ctx;
    uintptr_t bad = 0;

    ctx = ec_fd_get(fd, xl);
    if (ctx != NULL)
    {
        bad = ctx->bad;
    }

    return ~bad;
}

uintptr_t ec_update_inode(ec_fop_data_t * fop, inode_t * inode, uintptr_t good,
                          uintptr_t bad)
{
    ec_inode_t * ctx = NULL;

    if (inode != NULL)
    {
        LOCK(&inode->lock);

        ctx = __ec_inode_get(inode, fop->xl);
        if (ctx != NULL)
        {
            ctx->bad &= ~good;
            bad |= ctx->bad;
            ctx->bad = bad;
        }

        UNLOCK(&inode->lock);
    }

    return bad;
}

uintptr_t ec_update_fd(ec_fop_data_t * fop, fd_t * fd, uintptr_t good,
                       uintptr_t bad)
{
    ec_fd_t * ctx = NULL;

    LOCK(&fd->lock);

    ctx = __ec_fd_get(fd, fop->xl);
    if (ctx != NULL)
    {
        ctx->bad &= ~good;
        bad |= ctx->bad;
        ctx->bad = bad;
    }

    UNLOCK(&fd->lock);

    return bad;
}

int32_t ec_heal_report(call_frame_t * frame, void * cookie, xlator_t * this,
                       int32_t op_ret, int32_t op_errno, uintptr_t mask,
                       uintptr_t good, uintptr_t bad, dict_t * xdata)
{
    if (op_ret < 0) {
        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                EC_MSG_HEAL_FAIL, "Heal failed");
    } else {
        if ((mask & ~good) != 0) {
            gf_msg (this->name, GF_LOG_INFO, 0,
                    EC_MSG_HEAL_SUCCESS, "Heal succeeded on %d/%d "
                    "subvolumes",
                    ec_bits_count(mask & ~(good | bad)),
                    ec_bits_count(mask & ~good));
        }
    }

    return 0;
}

int32_t ec_fop_needs_heal(ec_fop_data_t *fop)
{
    ec_t *ec = fop->xl->private;

    return (ec->xl_up & ~(fop->remaining | fop->good)) != 0;
}

void ec_check_status(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    int32_t partial = 0;

    if (fop->answer->op_ret >= 0) {
        if ((fop->id == GF_FOP_LOOKUP) ||
            (fop->id == GF_FOP_STAT) || (fop->id == GF_FOP_FSTAT)) {
            partial = fop->answer->iatt[0].ia_type == IA_IFDIR;
        } else if (fop->id == GF_FOP_OPENDIR) {
            partial = 1;
        }
    }

    if (!ec_fop_needs_heal(fop)) {
        return;
    }

    gf_msg (fop->xl->name, GF_LOG_WARNING, 0,
            EC_MSG_OP_FAIL_ON_SUBVOLS,
            "Operation failed on some "
            "subvolumes (up=%lX, mask=%lX, "
            "remaining=%lX, good=%lX, bad=%lX)",
            ec->xl_up, fop->mask, fop->remaining, fop->good, fop->bad);

    if (fop->use_fd)
    {
        if (fop->fd != NULL) {
            ec_fheal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                     fop->fd, partial, NULL);
        }
    }
    else
    {
        ec_heal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                &fop->loc[0], partial, NULL);

        if (fop->loc[1].inode != NULL)
        {
            ec_heal(NULL, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                    &fop->loc[1], partial, NULL);
        }
    }
}

void ec_update_bad(ec_fop_data_t * fop, uintptr_t good)
{
    ec_t *ec = fop->xl->private;
    uintptr_t bad;

    /*Don't let fops that do dispatch_one() to update bad*/
    if (fop->expected == 1)
            return;

    bad = ec->xl_up & ~(fop->remaining | good);
    fop->bad |= bad;
    fop->good |= good;

    if (fop->parent == NULL)
    {
        if ((fop->flags & EC_FLAG_UPDATE_LOC_PARENT) != 0)
        {
            ec_update_inode(fop, fop->loc[0].parent, good, bad);
        }
        if ((fop->flags & EC_FLAG_UPDATE_LOC_INODE) != 0)
        {
            ec_update_inode(fop, fop->loc[0].inode, good, bad);
        }
        ec_update_inode(fop, fop->loc[1].inode, good, bad);
        if ((fop->flags & EC_FLAG_UPDATE_FD_INODE) != 0)
        {
            ec_update_inode(fop, fop->fd->inode, good, bad);
        }
        if ((fop->flags & EC_FLAG_UPDATE_FD) != 0)
        {
            ec_update_fd(fop, fop->fd, good, bad);
        }

        ec_check_status(fop);
    }
}


void __ec_fop_set_error(ec_fop_data_t * fop, int32_t error)
{
    if ((error != 0) && (fop->error == 0))
    {
        fop->error = error;
    }
}

void ec_fop_set_error(ec_fop_data_t * fop, int32_t error)
{
    LOCK(&fop->lock);

    __ec_fop_set_error(fop, error);

    UNLOCK(&fop->lock);
}

void ec_sleep(ec_fop_data_t *fop)
{
    LOCK(&fop->lock);

    GF_ASSERT (fop->refs > 0);
    fop->refs++;
    fop->jobs++;

    UNLOCK(&fop->lock);
}

int32_t ec_check_complete(ec_fop_data_t * fop, ec_resume_f resume)
{
    int32_t error = -1;

    LOCK(&fop->lock);

    GF_ASSERT(fop->resume == NULL);

    if (--fop->jobs != 0)
    {
        ec_trace("WAIT", fop, "resume=%p", resume);

        fop->resume = resume;
    }
    else
    {
        error = fop->error;
        fop->error = 0;
    }

    UNLOCK(&fop->lock);

    return error;
}

void ec_resume(ec_fop_data_t * fop, int32_t error)
{
    ec_resume_f resume = NULL;

    LOCK(&fop->lock);

    __ec_fop_set_error(fop, error);

    if (--fop->jobs == 0)
    {
        resume = fop->resume;
        fop->resume = NULL;
        if (resume != NULL)
        {
            ec_trace("RESUME", fop, "error=%d", error);

            if (fop->error != 0)
            {
                error = fop->error;
            }
            fop->error = 0;
        }
    }

    UNLOCK(&fop->lock);

    if (resume != NULL)
    {
        resume(fop, error);
    }

    ec_fop_data_release(fop);
}

void ec_resume_parent(ec_fop_data_t * fop, int32_t error)
{
    ec_fop_data_t * parent;

    parent = fop->parent;
    if (parent != NULL)
    {
        ec_trace("RESUME_PARENT", fop, "error=%u", error);
        fop->parent = NULL;
        ec_resume(parent, error);
    }
}

gf_boolean_t
ec_is_recoverable_error (int32_t op_errno)
{
        switch (op_errno) {
        case ENOTCONN:
        case ESTALE:
        case ENOENT:
        case EBADFD:/*Opened fd but brick is disconnected*/
        case EIO:/*Backend-fs crash like XFS/ext4 etc*/
                return _gf_true;
        }
        return _gf_false;
}

void ec_complete(ec_fop_data_t * fop)
{
    ec_cbk_data_t * cbk = NULL;
    int32_t resume = 0, update = 0;
    int healing_count = 0;

    LOCK(&fop->lock);

    ec_trace("COMPLETE", fop, "");

    if (--fop->winds == 0) {
        if (fop->answer == NULL) {
            if (!list_empty(&fop->cbk_list)) {
                cbk = list_entry(fop->cbk_list.next, ec_cbk_data_t, list);
                healing_count = ec_bits_count (cbk->mask & fop->healing);
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

    /* ec_update_bad() locks inode->lock. This may cause deadlocks with
       fop->lock when used in another order. Since ec_update_bad() will not
       be called more than once for each fop, it can be called from outside
       the fop->lock locked region. */
    if (update) {
        ec_update_bad(fop, cbk->mask);
    }

    if (resume)
    {
        ec_resume(fop, 0);
    }

    ec_fop_data_release(fop);
}

/* There could be already granted locks sitting on the bricks, unlock for which
 * must be wound at all costs*/
static gf_boolean_t
ec_must_wind (ec_fop_data_t *fop)
{
        if ((fop->id == GF_FOP_INODELK) || (fop->id == GF_FOP_FINODELK) ||
            (fop->id == GF_FOP_LK)) {
                if (fop->flock.l_type == F_UNLCK)
                        return _gf_true;
        } else if ((fop->id == GF_FOP_ENTRYLK) ||
                   (fop->id == GF_FOP_FENTRYLK)) {
                if (fop->entrylk_cmd == ENTRYLK_UNLOCK)
                        return _gf_true;
        }

        return _gf_false;
}

static gf_boolean_t
ec_internal_op (ec_fop_data_t *fop)
{
        if (ec_must_wind (fop))
                return _gf_true;
        if (fop->id == GF_FOP_XATTROP)
                return _gf_true;
        if (fop->id == GF_FOP_FXATTROP)
                return _gf_true;
        return _gf_false;
}

int32_t ec_child_select(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    uintptr_t mask = 0;
    int32_t first = 0, num = 0;

    ec_fop_cleanup(fop);

    fop->mask &= ec->node_mask;
    /* Wind the fop on same subvols as parent for any internal extra fops like
     * head/tail read in case of writev fop. Unlocks shouldn't do this because
     * unlock should go on all subvols where lock is performed*/
    if (fop->parent && !ec_internal_op (fop)) {
            fop->mask &= (fop->parent->mask & ~fop->parent->healing);
    }

    mask = ec->xl_up;
    if (fop->parent == NULL)
    {
        if ((fop->flags & EC_FLAG_UPDATE_LOC_PARENT) && fop->loc[0].parent)
            mask &= ec_inode_good(fop->loc[0].parent, fop->xl);

        if ((fop->flags & EC_FLAG_UPDATE_LOC_INODE) && fop->loc[0].inode) {
            mask &= ec_inode_good(fop->loc[0].inode, fop->xl);
        }

        if ((fop->flags & EC_FLAG_UPDATE_LOC_INODE) && fop->loc[1].inode) {
            mask &= ec_inode_good(fop->loc[1].inode, fop->xl);
        }

        if (fop->fd) {
            if ((fop->flags & EC_FLAG_UPDATE_FD_INODE) && fop->fd->inode) {
                mask &= ec_inode_good(fop->fd->inode, fop->xl);
            }
            if (fop->flags & fop->flags & EC_FLAG_UPDATE_FD) {
                    mask &= ec_fd_good(fop->fd, fop->xl);
            }
        }
    }

    if ((fop->mask & ~mask) != 0)
    {
        gf_msg (fop->xl->name, GF_LOG_WARNING, 0,
                EC_MSG_OP_EXEC_UNAVAIL,
                "Executing operation with "
                "some subvolumes unavailable "
                "(%lX)", fop->mask & ~mask);

        fop->mask &= mask;
    }

    switch (fop->minimum)
    {
        case EC_MINIMUM_ALL:
            fop->minimum = ec_bits_count(fop->mask);
            if (fop->minimum >= ec->fragments)
            {
                break;
            }
        case EC_MINIMUM_MIN:
            fop->minimum = ec->fragments;
            break;
        case EC_MINIMUM_ONE:
            fop->minimum = 1;
    }

    first = ec->idx;
    if (++first >= ec->nodes)
    {
        first = 0;
    }
    ec->idx = first;

    /*Unconditionally wind on healing subvolumes*/
    fop->mask |= fop->healing;
    fop->remaining = fop->mask;
    fop->received = 0;

    ec_trace("SELECT", fop, "");

    num = ec_bits_count(fop->mask);
    if ((num < fop->minimum) && (num < ec->fragments))
    {
        gf_msg (ec->xl->name, GF_LOG_ERROR, 0,
                EC_MSG_CHILDS_INSUFFICIENT,
                "Insufficient available childs "
                "for this request (have %d, need "
                "%d)", num, fop->minimum);

        return 0;
    }

    ec_sleep(fop);

    return 1;
}

int32_t ec_dispatch_next(ec_fop_data_t * fop, int32_t idx)
{
    ec_t * ec = fop->xl->private;

    LOCK(&fop->lock);

    idx = ec_child_next(ec, fop, idx);
    if (idx >= 0)
    {
        fop->remaining ^= 1ULL << idx;

        ec_trace("EXECUTE", fop, "idx=%d", idx);

        fop->winds++;
        fop->refs++;
    }

    UNLOCK(&fop->lock);

    if (idx >= 0)
    {
        fop->wind(ec, fop, idx);
    }

    return idx;
}

void ec_dispatch_mask(ec_fop_data_t * fop, uintptr_t mask)
{
    ec_t * ec = fop->xl->private;
    int32_t count, idx;

    count = ec_bits_count(mask);

    LOCK(&fop->lock);

    ec_trace("EXECUTE", fop, "mask=%lX", mask);

    fop->remaining ^= mask;

    fop->winds += count;
    fop->refs += count;

    UNLOCK(&fop->lock);

    idx = 0;
    while (mask != 0)
    {
        if ((mask & 1) != 0)
        {
            fop->wind(ec, fop, idx);
        }
        idx++;
        mask >>= 1;
    }
}

void ec_dispatch_start(ec_fop_data_t * fop)
{
    fop->answer = NULL;
    fop->good = 0;
    fop->bad = 0;

    INIT_LIST_HEAD(&fop->cbk_list);

    if (fop->lock_count > 0)
    {
        ec_owner_copy(fop->frame, &fop->req_frame->root->lk_owner);
    }
}

void ec_dispatch_one(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;

    ec_dispatch_start(fop);

    if (ec_child_select(fop))
    {
        fop->expected = 1;
        fop->first = ec->idx;

        ec_dispatch_next(fop, fop->first);
    }
}

gf_boolean_t
ec_dispatch_one_retry(ec_fop_data_t *fop, ec_cbk_data_t *cbk)
{
        if ((cbk->op_ret < 0) && ec_is_recoverable_error (cbk->op_errno)) {
                GF_ASSERT (fop->mask & (1ULL<<cbk->idx));
                fop->mask ^= (1ULL << cbk->idx);
                if (fop->mask)
                        return _gf_true;
        }
        return _gf_false;
}

void ec_dispatch_inc(ec_fop_data_t * fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop))
    {
        fop->expected = ec_bits_count(fop->remaining);
        fop->first = 0;

        ec_dispatch_next(fop, 0);
    }
}

void
ec_dispatch_all (ec_fop_data_t *fop)
{
        ec_dispatch_start(fop);

        if (ec_child_select(fop)) {
                fop->expected = ec_bits_count(fop->remaining);
                fop->first = 0;

                ec_dispatch_mask(fop, fop->remaining);
        }
}

void ec_dispatch_min(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    uintptr_t mask;
    int32_t idx, count;

    ec_dispatch_start(fop);

    if (ec_child_select(fop))
    {
        fop->expected = count = ec->fragments;
        fop->first = ec->idx;
        idx = fop->first - 1;
        mask = 0;
        while (count-- > 0)
        {
            idx = ec_child_next(ec, fop, idx + 1);
            mask |= 1ULL << idx;
        }

        ec_dispatch_mask(fop, mask);
    }
}

ec_lock_t *ec_lock_allocate(xlator_t *xl, loc_t *loc)
{
    ec_t * ec = xl->private;
    ec_lock_t * lock;

    if ((loc->inode == NULL) ||
        (gf_uuid_is_null(loc->gfid) && gf_uuid_is_null(loc->inode->gfid)))
    {
        gf_msg (xl->name, GF_LOG_ERROR, EINVAL,
                EC_MSG_INVALID_INODE,
                "Trying to lock based on an invalid "
                "inode");

        return NULL;
    }

    lock = mem_get0(ec->lock_pool);
    if (lock != NULL)
    {
        lock->good_mask = -1ULL;
        INIT_LIST_HEAD(&lock->waiting);
        INIT_LIST_HEAD(&lock->frozen);
        if (ec_loc_from_loc(xl, &lock->loc, loc) != 0)
        {
            mem_put(lock);
            lock = NULL;
        }
    }

    return lock;
}

void ec_lock_destroy(ec_lock_t * lock)
{
    loc_wipe(&lock->loc);
    if (lock->fd != NULL) {
        fd_unref(lock->fd);
    }

    mem_put(lock);
}

int32_t ec_lock_compare(ec_lock_t * lock1, ec_lock_t * lock2)
{
    return gf_uuid_compare(lock1->loc.gfid, lock2->loc.gfid);
}

void ec_lock_insert(ec_fop_data_t *fop, ec_lock_t *lock, uint32_t flags,
                    loc_t *base)
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

    lock->refs++;
    lock->inserted++;
}

void ec_lock_prepare_inode_internal(ec_fop_data_t *fop, loc_t *loc,
                                    uint32_t flags, loc_t *base)
{
    ec_lock_t *lock = NULL;
    ec_inode_t *ctx;

    if ((fop->parent != NULL) || (fop->error != 0) || (loc->inode == NULL)) {
        return;
    }

    LOCK(&loc->inode->lock);

    ctx = __ec_inode_get(loc->inode, fop->xl);
    if (ctx == NULL) {
        __ec_fop_set_error(fop, EIO);

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
            fop->locks[0].update[EC_METADATA_TXN] |=
                                                 (flags & EC_UPDATE_META) != 0;

            /* Only one base inode is allowed per fop, so there shouldn't be
             * overwrites here. */
            if (base != NULL) {
                fop->locks[0].base = base;
            }

            goto update_query;
        }

        ec_trace("LOCK_INODELK", fop, "lock=%p, inode=%p. Lock already "
                                      "acquired", lock, loc->inode);

        goto insert;
    }

    lock = ec_lock_allocate(fop->xl, loc);
    if (lock == NULL) {
        __ec_fop_set_error(fop, EIO);

        goto unlock;
    }

    ec_trace("LOCK_CREATE", fop, "lock=%p", lock);

    lock->flock.l_type = F_WRLCK;
    lock->flock.l_whence = SEEK_SET;

    lock->ctx = ctx;
    ctx->inode_lock = lock;

insert:
    ec_lock_insert(fop, lock, flags, base);
update_query:
    lock->query |= (flags & EC_QUERY_INFO) != 0;
unlock:
    UNLOCK(&loc->inode->lock);
}

void ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, uint32_t flags)
{
    ec_lock_prepare_inode_internal(fop, loc, flags, NULL);
}

void ec_lock_prepare_parent_inode(ec_fop_data_t *fop, loc_t *loc,
                                  uint32_t flags)
{
    loc_t tmp, *base = NULL;

    if (fop->error != 0) {
        return;
    }

    if (ec_loc_parent(fop->xl, loc, &tmp) != 0) {
        ec_fop_set_error(fop, EIO);

        return;
    }

    if ((flags & EC_INODE_SIZE) != 0) {
        base = loc;
        flags ^= EC_INODE_SIZE;
    }

    ec_lock_prepare_inode_internal(fop, &tmp, flags, base);

    loc_wipe(&tmp);
}

void ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, uint32_t flags)
{
    loc_t loc;

    if (fop->error != 0) {
        return;
    }

    if (ec_loc_from_fd(fop->xl, &loc, fd) != 0) {
        ec_fop_set_error(fop, EIO);

        return;
    }

    ec_lock_prepare_inode_internal(fop, &loc, flags, NULL);

    loc_wipe(&loc);
}

gf_boolean_t
ec_config_check (ec_fop_data_t *fop, ec_config_t *config)
{
    ec_t *ec;

    ec = fop->xl->private;
    if ((config->version != EC_CONFIG_VERSION) ||
        (config->algorithm != EC_CONFIG_ALGORITHM) ||
        (config->gf_word_size != EC_GF_BITS) ||
        (config->bricks != ec->nodes) ||
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
            ((config->chunk_size * 8) % (config->gf_word_size * data_bricks)
                                                                       != 0)) {
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_INVALID_CONFIG,
                    "Invalid or corrupted config");
        } else {
            gf_msg (fop->xl->name, GF_LOG_ERROR, EINVAL,
                    EC_MSG_INVALID_CONFIG,
                    "Unsupported config "
                    "(V=%u, A=%u, W=%u, "
                    "N=%u, R=%u, S=%u)",
                   config->version, config->algorithm,
                   config->gf_word_size, config->bricks,
                   config->redundancy, config->chunk_size);
        }

        return _gf_false;
    }

    return _gf_true;
}

int32_t
ec_prepare_update_cbk (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *dict, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie, *parent;
    ec_lock_link_t *link = fop->data;
    ec_lock_t *lock = NULL;
    ec_inode_t *ctx;

    lock = link->lock;
    parent = link->fop;
    ctx = lock->ctx;

    if (op_ret < 0) {
        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                EC_MSG_SIZE_VERS_GET_FAIL,
                "Failed to get size and version");

        goto out;
    }

    op_errno = EIO;

    LOCK(&lock->loc.inode->lock);

    if (ec_dict_del_array(dict, EC_XATTR_VERSION, ctx->pre_version,
                          EC_VERSION_SIZE) != 0) {
        gf_msg (this->name, GF_LOG_ERROR, 0,
                EC_MSG_VER_XATTR_GET_FAIL,
                "Unable to get version xattr");

        goto unlock;
    }
    ctx->post_version[0] += ctx->pre_version[0];
    ctx->post_version[1] += ctx->pre_version[1];

    ctx->have_version = _gf_true;

    if (lock->loc.inode->ia_type == IA_IFREG) {
        if (ec_dict_del_number(dict, EC_XATTR_SIZE, &ctx->pre_size) != 0) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_SIZE_XATTR_GET_FAIL, "Unable to get size xattr");

            goto unlock;
        }
        ctx->post_size = ctx->pre_size;

        ctx->have_size = _gf_true;

        if ((ec_dict_del_config(dict, EC_XATTR_CONFIG, &ctx->config) != 0) ||
            !ec_config_check(parent, &ctx->config)) {
            gf_msg (this->name, GF_LOG_ERROR, 0,
                    EC_MSG_CONFIG_XATTR_GET_FAIL,
                    "Unable to get config xattr");

            goto unlock;
        }

        ctx->have_config = _gf_true;
    }

    ctx->have_info = _gf_true;

    op_errno = 0;

unlock:
    UNLOCK(&lock->loc.inode->lock);
out:
    if (op_errno == 0) {
        parent->mask &= fop->good;

        /*As of now only data healing marks bricks as healing*/
        lock->healing |= fop->healing;
        if (ec_is_data_fop (parent->id)) {
            parent->healing |= fop->healing;
        }
    } else {
        ec_fop_set_error(parent, op_errno);
    }

    return 0;
}

void ec_get_size_version(ec_lock_link_t *link)
{
    loc_t loc;
    ec_lock_t *lock;
    ec_inode_t *ctx;
    ec_fop_data_t *fop;
    dict_t *dict = NULL;
    uid_t uid;
    gid_t gid;
    int32_t error = ENOMEM;
    uint64_t allzero[EC_VERSION_SIZE] = {0, 0};

    lock = link->lock;
    ctx = lock->ctx;
    fop = link->fop;

    /* If ec metadata has already been retrieved, do not try again. */
    if (ctx->have_info) {
        if (ec_is_data_fop (fop->id)) {
            fop->healing |= lock->healing;
        }
        return;
    }

    /* Determine if there's something we need to retrieve for the current
     * operation. */
    if (!lock->query && (lock->loc.inode->ia_type != IA_IFREG)) {
        return;
    }

    uid = fop->frame->root->uid;
    gid = fop->frame->root->gid;

    memset(&loc, 0, sizeof(loc));

    dict = dict_new();
    if (dict == NULL) {
        goto out;
    }

    /* Once we know that an xattrop will be needed, we try to get all available
     * information in a single call. */
    if ((ec_dict_set_array(dict, EC_XATTR_VERSION, allzero,
                          EC_VERSION_SIZE) != 0) ||
        (ec_dict_set_array(dict, EC_XATTR_DIRTY, allzero,
                           EC_VERSION_SIZE) != 0)) {
        goto out;
    }

    if (lock->loc.inode->ia_type == IA_IFREG) {
        if ((ec_dict_set_number(dict, EC_XATTR_SIZE, 0) != 0) ||
            (ec_dict_set_number(dict, EC_XATTR_CONFIG, 0) != 0)) {
            goto out;
        }
    }

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    /* For normal fops, ec_[f]xattrop() must succeed on at least
     * EC_MINIMUM_MIN bricks, however when this is called as part of a
     * self-heal operation the mask of target bricks (fop->mask) could
     * contain less than EC_MINIMUM_MIN bricks, causing the lookup to
     * always fail. Thus we always use the same minimum used for the main
     * fop.
     */
    if (lock->fd == NULL) {
        if (ec_loc_from_loc(fop->xl, &loc, &lock->loc) != 0) {
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

        ec_xattrop (fop->frame, fop->xl, fop->mask, fop->minimum,
                    ec_prepare_update_cbk, link, &loc,
                    GF_XATTROP_ADD_ARRAY64, dict, NULL);
    } else {
        ec_fxattrop(fop->frame, fop->xl, fop->mask, fop->minimum,
                ec_prepare_update_cbk, link, lock->fd,
                GF_XATTROP_ADD_ARRAY64, dict, NULL);
    }

    error = 0;

out:
    fop->frame->root->uid = uid;
    fop->frame->root->gid = gid;

    loc_wipe(&loc);

    if (dict != NULL) {
        dict_unref(dict);
    }

    if (error != 0) {
        ec_fop_set_error(fop, error);
    }
}

gf_boolean_t ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode,
                               uint64_t *size)
{
    ec_inode_t *ctx;
    gf_boolean_t found = _gf_false;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto unlock;
    }

    if (ctx->have_size) {
        *size = ctx->post_size;
        found = _gf_true;
    }

unlock:
    UNLOCK(&inode->lock);

    return found;
}

gf_boolean_t ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode,
                               uint64_t size)
{
    ec_inode_t *ctx;
    gf_boolean_t found = _gf_false;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto unlock;
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

unlock:
    UNLOCK(&inode->lock);

    return found;
}

void ec_clear_inode_info(ec_fop_data_t *fop, inode_t *inode)
{
    ec_inode_t *ctx;

    LOCK(&inode->lock);

    ctx = __ec_inode_get(inode, fop->xl);
    if (ctx == NULL) {
        goto unlock;
    }

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

int32_t ec_get_real_size_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, inode_t *inode,
                             struct iatt *buf, dict_t *xdata,
                             struct iatt *postparent)
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
 * no lock is needed on the inode. This is only required to maintan iatt
 * structs on fops that manipulate directory entries but do not operate
 * directly on the inode, like link, rename, ...
 *
 * Any error processing this request is ignored. In the worst case, an invalid
 * or not up to date value in the iatt could cause some cache invalidation.
 */
void ec_get_real_size(ec_lock_link_t *link)
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

void ec_lock_acquired(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_fop_data_t *fop;

    lock = link->lock;
    fop = link->fop;

    ec_trace("LOCKED", link->fop, "lock=%p", lock);

    /* If the fop has an fd available, attach it to the lock structure to be
     * able to do fxattrop calls instead of xattrop. It's safe to change this
     * here because no xattrop using the fd can start concurrently at this
     * point. */
    if (fop->use_fd) {
        if (lock->fd != NULL) {
            fd_unref(lock->fd);
        }
        lock->fd = fd_ref(fop->fd);
    }
    lock->acquired = _gf_true;

    fop->mask &= lock->good_mask;

    fop->locked++;

    ec_get_size_version(link);
    ec_get_real_size(link);
}

int32_t ec_locked(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link = NULL;
    ec_lock_t *lock = NULL;

    if (op_ret >= 0) {
        link = fop->data;
        lock = link->lock;
        lock->mask = lock->good_mask = fop->good;
        lock->healing = 0;

        ec_lock_acquired(link);
        ec_lock(fop->parent);
    } else {
        gf_msg (this->name, GF_LOG_WARNING, 0,
                EC_MSG_PREOP_LOCK_FAILED,
                "Failed to complete preop lock");
    }

    return 0;
}

gf_boolean_t ec_lock_acquire(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_fop_data_t *fop;

    lock = link->lock;
    fop = link->fop;
    if (!lock->acquired) {
        ec_owner_set(fop->frame, lock);

        ec_trace("LOCK_ACQUIRE", fop, "lock=%p, inode=%p", lock,
                 lock->loc.inode);

        lock->flock.l_type = F_WRLCK;
        ec_inodelk(fop->frame, fop->xl, -1, EC_MINIMUM_ALL, ec_locked,
                   link, fop->xl->name, &lock->loc, F_SETLKW, &lock->flock,
                   NULL);

        return _gf_false;
    }

    ec_trace("LOCK_REUSE", fop, "lock=%p", lock);

    ec_lock_acquired(link);

    return _gf_true;
}

void ec_lock(ec_fop_data_t *fop)
{
    ec_lock_link_t *link;
    ec_lock_link_t *timer_link = NULL;
    ec_lock_t *lock;

    /* There is a chance that ec_resume is called on fop even before ec_sleep.
     * Which can result in refs == 0 for fop leading to use after free in this
     * function when it calls ec_sleep so do ec_sleep at start and ec_resume at
     * the end of this function.*/
    ec_sleep (fop);

    while (fop->locked < fop->lock_count) {
        /* Since there are only up to 2 locks per fop, this xor will change
         * the order of the locks if fop->first_lock is 1. */
        link = &fop->locks[fop->locked ^ fop->first_lock];
        lock = link->lock;

        timer_link = NULL;

        LOCK(&lock->loc.inode->lock);
        GF_ASSERT (lock->inserted > 0);
        lock->inserted--;

        if (lock->timer != NULL) {
            GF_ASSERT (lock->release == _gf_false);
            timer_link = lock->timer->data;
            if (gf_timer_call_cancel(fop->xl->ctx, lock->timer) == 0) {
                    ec_trace("UNLOCK_CANCELLED", timer_link->fop,
                             "lock=%p", lock);
                    lock->timer = NULL;
                    lock->refs--;
                    /* There should remain at least 1 ref, the current one. */
                    GF_ASSERT(lock->refs > 0);
            } else {
                    /* Timer expired and on the way to unlock.
                     * Set lock->release to _gf_true, so that this
                     * lock will be put in frozen list*/
                    timer_link = NULL;
                    lock->release = _gf_true;
            }
        }

        GF_ASSERT(list_empty(&link->wait_list));

        if ((lock->owner != NULL) || lock->release) {
            if (lock->release) {
                ec_trace("LOCK_QUEUE_FREEZE", fop, "lock=%p", lock);

                list_add_tail(&link->wait_list, &lock->frozen);

                /* The lock is frozen, so we move the current reference to
                 * refs_frozen. After that, there should remain at least one
                 * ref belonging to the lock that is processing the release. */
                lock->refs--;
                GF_ASSERT(lock->refs > 0);
                lock->refs_frozen++;
            } else {
                ec_trace("LOCK_QUEUE_WAIT", fop, "lock=%p", lock);

                list_add_tail(&link->wait_list, &lock->waiting);
            }

            UNLOCK(&lock->loc.inode->lock);

            ec_sleep(fop);

            break;
        }

        lock->owner = fop;

        UNLOCK(&lock->loc.inode->lock);

        if (!ec_lock_acquire(link)) {
            break;
        }

        if (timer_link != NULL) {
            ec_resume(timer_link->fop, 0);
            timer_link = NULL;
        }
    }
    ec_resume (fop, 0);

    if (timer_link != NULL) {
        ec_resume(timer_link->fop, 0);
    }
}

void
ec_lock_unfreeze(ec_lock_link_t *link)
{
    ec_lock_t *lock;

    lock = link->lock;

    LOCK(&lock->loc.inode->lock);

    lock->acquired = _gf_false;
    lock->release = _gf_false;

    lock->refs--;
    GF_ASSERT (lock->refs == lock->inserted);

    GF_ASSERT(list_empty(&lock->waiting) && (lock->owner == NULL));

    list_splice_init(&lock->frozen, &lock->waiting);
    lock->refs += lock->refs_frozen;
    lock->refs_frozen = 0;

    if (!list_empty(&lock->waiting)) {
        link = list_entry(lock->waiting.next, ec_lock_link_t, wait_list);
        list_del_init(&link->wait_list);

        lock->owner = link->fop;

        UNLOCK(&lock->loc.inode->lock);

        ec_trace("LOCK_UNFREEZE", link->fop, "lock=%p", lock);

        if (ec_lock_acquire(link)) {
            ec_lock(link->fop);
        }
        ec_resume(link->fop, 0);
    } else if (lock->refs == 0) {
        ec_trace("LOCK_DESTROY", link->fop, "lock=%p", lock);

        lock->ctx->inode_lock = NULL;

        UNLOCK(&lock->loc.inode->lock);

        ec_lock_destroy(lock);
    } else {
        UNLOCK(&lock->loc.inode->lock);
    }
}

int32_t ec_unlocked(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link = fop->data;

    if (op_ret < 0) {
        gf_msg (this->name, GF_LOG_WARNING, 0,
                EC_MSG_UNLOCK_FAILED,
                "entry/inode unlocking failed (%s)",
                ec_fop_name(link->fop->id));
    } else {
        ec_trace("UNLOCKED", link->fop, "lock=%p", link->lock);
    }

    ec_lock_unfreeze(link);

    return 0;
}

void ec_unlock_lock(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_fop_data_t *fop;

    lock = link->lock;
    fop = link->fop;

    ec_clear_inode_info(fop, lock->loc.inode);

    if ((lock->mask != 0) && lock->acquired) {
        ec_owner_set(fop->frame, lock);

        lock->flock.l_type = F_UNLCK;
        ec_trace("UNLOCK_INODELK", fop, "lock=%p, inode=%p", lock,
                 lock->loc.inode);

        ec_inodelk(fop->frame, fop->xl, lock->mask, EC_MINIMUM_ONE,
                   ec_unlocked, link, fop->xl->name, &lock->loc, F_SETLK,
                   &lock->flock, NULL);
    } else {
        ec_lock_unfreeze(link);
    }
}

int32_t ec_update_size_version_done(call_frame_t * frame, void * cookie,
                                    xlator_t * this, int32_t op_ret,
                                    int32_t op_errno, dict_t * xattr,
                                    dict_t * xdata)
{
    ec_fop_data_t *fop = cookie;
    ec_lock_link_t *link;
    ec_lock_t *lock;
    ec_inode_t *ctx;

    if (op_ret < 0) {
        gf_msg(fop->xl->name, fop_log_level (fop->id, op_errno), op_errno,
               EC_MSG_SIZE_VERS_UPDATE_FAIL,
               "Failed to update version and size");
    } else {
        fop->parent->mask &= fop->good;
        link = fop->data;
        lock = link->lock;
        ctx = lock->ctx;

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
            ec_config_check(fop->parent, &ctx->config)) {
            ctx->have_config = _gf_true;
        }

        ctx->have_info = _gf_true;
    }

    if ((fop->parent->id != GF_FOP_FLUSH) &&
        (fop->parent->id != GF_FOP_FSYNC) &&
        (fop->parent->id != GF_FOP_FSYNCDIR)) {
        ec_unlock_lock(fop->data);
    }

    return 0;
}

void
ec_update_size_version(ec_lock_link_t *link, uint64_t *version,
                       uint64_t size, uint64_t *dirty)
{
    ec_fop_data_t *fop;
    ec_lock_t *lock;
    ec_inode_t *ctx;
    dict_t * dict;
    uid_t uid;
    gid_t gid;

    fop = link->fop;

    ec_trace("UPDATE", fop, "version=%ld/%ld, size=%ld, dirty=%ld/%ld",
             version[0], version[1], size, dirty[0], dirty[1]);

    dict = dict_new();
    if (dict == NULL) {
        goto out;
    }

    lock = link->lock;
    ctx = lock->ctx;

    /* If we don't have version information or it has been modified, we
     * update it. */
    if (!ctx->have_version || (version[0] != 0) || (version[1] != 0)) {
        if (ec_dict_set_array(dict, EC_XATTR_VERSION,
                              version, EC_VERSION_SIZE) != 0) {
            goto out;
        }
    }

    if (size != 0) {
        /* If size has been changed, we should already know the previous size
         * of the file. */
        GF_ASSERT(ctx->have_size);

        if (ec_dict_set_number(dict, EC_XATTR_SIZE, size) != 0) {
            goto out;
        }
    }

    /* If we don't have dirty information or it has been modified, we update
     * it. */
    if ((dirty[0] != 0) || (dirty[1] != 0)) {
        if (ec_dict_set_array(dict, EC_XATTR_DIRTY, dirty,
                              EC_VERSION_SIZE) != 0) {
            goto out;
        }
    }

    /* If config information is not know, we request it now. */
    if ((lock->loc.inode->ia_type == IA_IFREG) && !ctx->have_config) {
        /* A failure requesting this xattr is ignored because it's not
         * absolutely required right now. */
        ec_dict_set_number(dict, EC_XATTR_CONFIG, 0);
    }

    uid = fop->frame->root->uid;
    gid = fop->frame->root->gid;

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    if (link->lock->fd == NULL) {
            ec_xattrop(fop->frame, fop->xl, fop->mask, EC_MINIMUM_MIN,
                       ec_update_size_version_done, link, &link->lock->loc,
                       GF_XATTROP_ADD_ARRAY64, dict, NULL);
    } else {
            ec_fxattrop(fop->frame, fop->xl, fop->mask, EC_MINIMUM_MIN,
                       ec_update_size_version_done, link, link->lock->fd,
                       GF_XATTROP_ADD_ARRAY64, dict, NULL);
    }

    fop->frame->root->uid = uid;
    fop->frame->root->gid = gid;

    dict_unref(dict);

    return;

out:
    if (dict != NULL) {
        dict_unref(dict);
    }

    ec_fop_set_error(fop, EIO);

    gf_msg (fop->xl->name, GF_LOG_ERROR, 0,
            EC_MSG_SIZE_VERS_UPDATE_FAIL,
            "Unable to update version and size");
}

gf_boolean_t
ec_update_info(ec_lock_link_t *link)
{
    ec_lock_t *lock;
    ec_inode_t *ctx;
    uint64_t version[2];
    uint64_t dirty[2];
    uint64_t size;

    lock = link->lock;
    ctx = lock->ctx;

    /* pre_version[*] will be 0 if have_version is false */
    version[0] = ctx->post_version[0] - ctx->pre_version[0];
    version[1] = ctx->post_version[1] - ctx->pre_version[1];

    size = ctx->post_size - ctx->pre_size;

    dirty[0] = ctx->dirty[0];
    dirty[1] = ctx->dirty[1];
    /*Dirty is not combined so just reset it right here*/
    memset(ctx->dirty, 0, sizeof(ctx->dirty));

    if ((version[0] != 0) || (version[1] != 0) ||
        (dirty[0] != 0) || (dirty[1] != 0)) {
        ec_update_size_version(link, version, size, dirty);

        return _gf_true;
    }

    return _gf_false;
}

void
ec_unlock_now(ec_lock_link_t *link)
{
    ec_trace("UNLOCK_NOW", link->fop, "lock=%p", link->lock);

    if (!ec_update_info(link)) {
        ec_unlock_lock(link);
    }

    ec_resume(link->fop, 0);
}

void
ec_unlock_timer_del(ec_lock_link_t *link)
{
        int32_t before = 0;
        ec_lock_t *lock;
        inode_t *inode;
        gf_boolean_t now = _gf_false;

        lock = link->lock;

        /* A race condition can happen if timer expires, calls this function
         * and the lock is released (lock->loc is wiped) but the fop is not
         * fully completed yet (it's still on the list of pending fops). In
         * this case, this function can also be called if ec_unlock_force() is
         * called. */
        inode = lock->loc.inode;
        if (inode == NULL) {
                return;
        }

        LOCK(&inode->lock);

        if (lock->timer != NULL) {
                ec_trace("UNLOCK_DELAYED", link->fop, "lock=%p", lock);

                gf_timer_call_cancel(link->fop->xl->ctx, lock->timer);
                lock->timer = NULL;

                lock->release = now = _gf_true;

                before = lock->refs + lock->refs_frozen;
                list_splice_init(&lock->waiting, &lock->frozen);
                lock->refs_frozen += lock->refs - lock->inserted - 1;
                lock->refs = 1 + lock->inserted;
                /* We moved around the locks, so total number of locks shouldn't
                 * change by this operation*/
                GF_ASSERT (before == (lock->refs + lock->refs_frozen));
        }

        UNLOCK(&inode->lock);

        if (now) {
                ec_unlock_now(link);
        }
}

void ec_unlock_timer_cbk(void *data)
{
        ec_unlock_timer_del(data);
}

void ec_unlock_timer_add(ec_lock_link_t *link)
{
    struct timespec delay;
    ec_fop_data_t *fop = link->fop;
    ec_lock_t *lock = link->lock;
    gf_boolean_t now = _gf_false;

    LOCK(&lock->loc.inode->lock);

    GF_ASSERT(lock->timer == NULL);

    if ((lock->refs - lock->inserted) > 1) {
        ec_trace("UNLOCK_SKIP", fop, "lock=%p", lock);

        lock->refs--;

        UNLOCK(&lock->loc.inode->lock);
    } else if (lock->acquired) {
        ec_t *ec = fop->xl->private;

        ec_sleep(fop);

        /* If healing is needed, the lock needs to be released due to
         * contention, or ec is shutting down, do not delay lock release. */
        if (!lock->release && !ec_fop_needs_heal(fop) && !ec->shutdown) {
            ec_trace("UNLOCK_DELAY", fop, "lock=%p, release=%d", lock,
                     lock->release);

            delay.tv_sec = 1;
            delay.tv_nsec = 0;
            lock->timer = gf_timer_call_after(fop->xl->ctx, delay,
                                              ec_unlock_timer_cbk, link);
            if (lock->timer == NULL) {
                gf_msg(fop->xl->name, GF_LOG_WARNING, 0,
                       EC_MSG_UNLOCK_DELAY_FAILED,
                       "Unable to delay an "
                       "unlock");

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
        lock->release = _gf_true;

        UNLOCK(&lock->loc.inode->lock);

        ec_lock_unfreeze(link);
    }
}

void ec_unlock(ec_fop_data_t *fop)
{
    int32_t i;

    for (i = 0; i < fop->lock_count; i++) {
        ec_unlock_timer_add(&fop->locks[i]);
    }
}

void ec_flush_size_version(ec_fop_data_t * fop)
{
    GF_ASSERT(fop->lock_count == 1);

    ec_update_info(&fop->locks[0]);
}

void ec_lock_reuse(ec_fop_data_t *fop)
{
    ec_t *ec;
    ec_cbk_data_t *cbk;
    ec_lock_t *lock;
    ec_lock_link_t *link;
    ec_inode_t *ctx;
    int32_t i, count;
    gf_boolean_t release = _gf_false;

    cbk = fop->answer;
    if (cbk != NULL) {
        if (cbk->xdata != NULL) {
            if ((dict_get_int32(cbk->xdata, GLUSTERFS_INODELK_COUNT,
                                &count) == 0) && (count > 1)) {
                release = _gf_true;
            }
            if (release) {
                gf_msg_debug (fop->xl->name, 0,
                       "Lock contention detected");
            }
        }
    } else {
        /* If we haven't get an answer with enough quorum, we always release
         * the lock. */
        release = _gf_true;
    }

    ec = fop->xl->private;

    for (i = 0; i < fop->lock_count; i++)
    {
        link = &fop->locks[i];
        lock = link->lock;
        ctx = lock->ctx;

        LOCK(&lock->loc.inode->lock);

        ec_trace("LOCK_DONE", fop, "lock=%p", lock);

        GF_ASSERT(lock->owner == fop);
        lock->owner = NULL;
        lock->release |= release;

        if ((fop->error == 0) && (cbk != NULL) && (cbk->op_ret >= 0)) {
            if (link->update[0]) {
                ctx->post_version[0]++;
                if (ec->node_mask & ~fop->mask) {
                    ctx->dirty[0]++;
                }
            }
            if (link->update[1]) {
                ctx->post_version[1]++;
                if (ec->node_mask & ~fop->mask) {
                    ctx->dirty[1]++;
                }
            }
        }

        lock->good_mask &= fop->mask;

        link = NULL;
        if (!list_empty(&lock->waiting))
        {
            link = list_entry(lock->waiting.next, ec_lock_link_t, wait_list);
            list_del_init(&link->wait_list);

            lock->owner = link->fop;
        }

        UNLOCK(&lock->loc.inode->lock);

        if (link != NULL)
        {
            if (ec_lock_acquire(link)) {
                ec_lock(link->fop);
            }
            ec_resume(link->fop, 0);
        }
    }
}

void __ec_manager(ec_fop_data_t * fop, int32_t error)
{
    ec_t *ec = fop->xl->private;

    do {
        ec_trace("MANAGER", fop, "error=%d", error);

        if (!ec_must_wind (fop)) {
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
        GF_ASSERT (fop->state >= 0);

        error = ec_check_complete(fop, __ec_manager);
    } while (error >= 0);
}

void ec_manager(ec_fop_data_t * fop, int32_t error)
{
    GF_ASSERT(fop->jobs == 0);
    GF_ASSERT(fop->winds == 0);
    GF_ASSERT(fop->error == 0);

    if (fop->state == EC_STATE_START)
    {
        fop->state = EC_STATE_INIT;
    }

    __ec_manager(fop, error);
}
