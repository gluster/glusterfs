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

#include "byte-order.h"

#include "ec-mem-types.h"
#include "ec-data.h"
#include "ec-helpers.h"
#include "ec-combine.h"
#include "ec-common.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec.h"

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
    if (op_ret < 0)
    {
        gf_log(this->name, GF_LOG_WARNING, "Heal failed (error %d)", op_errno);
    }
    else
    {
        gf_log(this->name, GF_LOG_INFO, "Heal succeeded on %d/%d subvolumes",
               ec_bits_count(mask & ~ (good | bad)),
               ec_bits_count(mask & ~good));
    }

    return 0;
}

void ec_check_status(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    int32_t partial = 0;

    if (fop->answer->op_ret >= 0) {
        if (fop->id == GF_FOP_LOOKUP) {
            partial = fop->answer->iatt[0].ia_type == IA_IFDIR;
        } else if (fop->id == GF_FOP_OPENDIR) {
            partial = 1;
        }
    }

    if ((ec->xl_up & ~(fop->remaining | fop->good)) == 0)
    {
        return;
    }

    gf_log(fop->xl->name, GF_LOG_WARNING, "Operation failed on some "
                                          "subvolumes (up=%lX, mask=%lX, "
                                          "remaining=%lX, good=%lX, bad=%lX)",
           ec->xl_up, fop->mask, fop->remaining, fop->good, fop->bad);

    if (fop->use_fd)
    {
        if (fop->fd != NULL) {
            ec_fheal(fop->frame, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report,
                     NULL, fop->fd, partial, NULL);
        }
    }
    else
    {
        ec_heal(fop->frame, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report, NULL,
                &fop->loc[0], partial, NULL);

        if (fop->loc[1].inode != NULL)
        {
            ec_heal(fop->frame, fop->xl, -1, EC_MINIMUM_ONE, ec_heal_report,
                    NULL, &fop->loc[1], partial, NULL);
        }
    }
}

void ec_update_bad(ec_fop_data_t * fop, uintptr_t good)
{
    ec_t *ec = fop->xl->private;
    uintptr_t bad;

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

int32_t ec_check_complete(ec_fop_data_t * fop, ec_resume_f resume)
{
    int32_t error = -1;

    LOCK(&fop->lock);

    GF_ASSERT(fop->resume == NULL);

    if (fop->jobs != 0)
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

void ec_wait_winds(ec_fop_data_t * fop)
{
    LOCK(&fop->lock);

    if (fop->winds > 0)
    {
        fop->jobs++;
        fop->refs++;

        fop->flags |= EC_FLAG_WAITING_WINDS;
    }

    UNLOCK(&fop->lock);
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
        fop->parent = NULL;
        ec_resume(parent, error);
    }
}

void ec_complete(ec_fop_data_t * fop)
{
    ec_cbk_data_t * cbk = NULL;
    int32_t resume = 0;

    LOCK(&fop->lock);

    ec_trace("COMPLETE", fop, "");

    if (--fop->winds == 0)
    {
        if (fop->answer == NULL)
        {
            if (!list_empty(&fop->cbk_list))
            {
                cbk = list_entry(fop->cbk_list.next, ec_cbk_data_t, list);
                if ((cbk->count >= fop->minimum) &&
                    ((cbk->op_ret >= 0) || (cbk->op_errno != ENOTCONN)))
                {
                    fop->answer = cbk;

                    ec_update_bad(fop, cbk->mask);
                }
            }

            resume = 1;
        }
        else if ((fop->flags & EC_FLAG_WAITING_WINDS) != 0)
        {
            resume = 1;
        }
    }

    UNLOCK(&fop->lock);

    if (resume)
    {
        ec_resume(fop, 0);
    }

    ec_fop_data_release(fop);
}

int32_t ec_child_select(ec_fop_data_t * fop)
{
    ec_t * ec = fop->xl->private;
    uintptr_t mask = 0;
    int32_t first = 0, num = 0;

    fop->mask &= ec->node_mask;

    mask = ec->xl_up;
    if (fop->parent == NULL)
    {
        if (fop->loc[0].inode != NULL) {
            mask &= ec_inode_good(fop->loc[0].inode, fop->xl);
        }
        if (fop->loc[1].inode != NULL) {
            mask &= ec_inode_good(fop->loc[1].inode, fop->xl);
        }
        if (fop->fd != NULL) {
            if (fop->fd->inode != NULL) {
                mask &= ec_inode_good(fop->fd->inode, fop->xl);
            }
            mask &= ec_fd_good(fop->fd, fop->xl);
        }
    }
    if ((fop->mask & ~mask) != 0)
    {
        gf_log(fop->xl->name, GF_LOG_WARNING, "Executing operation with "
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

    fop->remaining = fop->mask;

    ec_trace("SELECT", fop, "");

    num = ec_bits_count(fop->mask);
    if ((num < fop->minimum) && (num < ec->fragments))
    {
        gf_log(ec->xl->name, GF_LOG_ERROR, "Insufficient available childs "
                                           "for this request (have %d, need "
                                           "%d)", num, fop->minimum);

        return 0;
    }

    LOCK(&fop->lock);

    fop->jobs++;
    fop->refs++;

    UNLOCK(&fop->lock);

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

int32_t ec_dispatch_one_retry(ec_fop_data_t * fop, int32_t idx, int32_t op_ret,
                              int32_t op_errno)
{
    if ((op_ret < 0) && (op_errno == ENOTCONN))
    {
        return (ec_dispatch_next(fop, idx) >= 0);
    }

    return 0;
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

void ec_dispatch_all(ec_fop_data_t * fop)
{
    ec_dispatch_start(fop);

    if (ec_child_select(fop))
    {
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

ec_lock_t * ec_lock_allocate(xlator_t * xl, int32_t kind, loc_t * loc)
{
    ec_t * ec = xl->private;
    ec_lock_t * lock;

    if ((loc->inode == NULL) ||
        (uuid_is_null(loc->gfid) && uuid_is_null(loc->inode->gfid)))
    {
        gf_log(xl->name, GF_LOG_ERROR, "Trying to lock based on an invalid "
                                       "inode");

        return NULL;
    }

    lock = mem_get0(ec->lock_pool);
    if (lock != NULL)
    {
        lock->kind = kind;
        lock->good_mask = -1ULL;
        INIT_LIST_HEAD(&lock->waiting);
        if (!ec_loc_from_loc(xl, &lock->loc, loc))
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

    mem_put(lock);
}

int32_t ec_lock_compare(ec_lock_t * lock1, ec_lock_t * lock2)
{
    return uuid_compare(lock1->loc.gfid, lock2->loc.gfid);
}

ec_lock_link_t *ec_lock_insert(ec_fop_data_t *fop, ec_lock_t *lock,
                               int32_t update)
{
    ec_lock_t * tmp;
    ec_lock_link_t *link = NULL;
    int32_t tmp_update;

    if ((fop->lock_count > 0) &&
        (ec_lock_compare(fop->locks[0].lock, lock) > 0))
    {
        tmp = fop->locks[0].lock;
        fop->locks[0].lock = lock;
        lock = tmp;

        tmp_update = fop->locks_update;
        fop->locks_update = update;
        update = tmp_update;
    }
    fop->locks[fop->lock_count].lock = lock;
    fop->locks[fop->lock_count].fop = fop;

    fop->locks_update |= update << fop->lock_count;

    fop->lock_count++;

    if (lock->timer != NULL) {
        link = lock->timer->data;
        ec_trace("UNLOCK_CANCELLED", link->fop, "lock=%p", lock);
        gf_timer_call_cancel(fop->xl->ctx, lock->timer);
        lock->timer = NULL;
    } else {
        lock->refs++;
    }

    return link;
}

void ec_lock_prepare_entry(ec_fop_data_t *fop, loc_t *loc, int32_t update)
{
    ec_lock_t * lock = NULL;
    ec_inode_t * ctx = NULL;
    ec_lock_link_t *link = NULL;
    loc_t tmp;
    int32_t error;

    if ((fop->parent != NULL) || (fop->error != 0))
    {
        return;
    }

    /* update is only 0 for 'opendir', which needs to lock the entry pointed
     * by loc instead of its parent.
     */
    if (update)
    {
        error = ec_loc_parent(fop->xl, loc, &tmp);
        if (error != 0) {
            ec_fop_set_error(fop, error);

            return;
        }
    } else {
        if (!ec_loc_from_loc(fop->xl, &tmp, loc)) {
            ec_fop_set_error(fop, EIO);

            return;
        }
    }

    LOCK(&tmp.inode->lock);

    ctx = __ec_inode_get(tmp.inode, fop->xl);
    if (ctx == NULL)
    {
        __ec_fop_set_error(fop, EIO);

        goto unlock;
    }

    if (ctx->entry_lock != NULL)
    {
        lock = ctx->entry_lock;
        ec_trace("LOCK_ENTRYLK", fop, "lock=%p, inode=%p, path=%s"
                                      "Lock already acquired",
                 lock, tmp.inode, tmp.path);

        goto insert;
    }

    lock = ec_lock_allocate(fop->xl, EC_LOCK_ENTRY, &tmp);
    if (lock == NULL)
    {
        __ec_fop_set_error(fop, EIO);

        goto unlock;
    }

    ec_trace("LOCK_CREATE", fop, "lock=%p", lock);

    lock->type = ENTRYLK_WRLCK;

    lock->plock = &ctx->entry_lock;
    ctx->entry_lock = lock;

insert:
    link = ec_lock_insert(fop, lock, update);

unlock:
    UNLOCK(&tmp.inode->lock);

    loc_wipe(&tmp);

    if (link != NULL) {
        ec_resume(link->fop, 0);
    }
}

void ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, int32_t update)
{
    ec_lock_link_t *link = NULL;
    ec_lock_t * lock;
    ec_inode_t * ctx;

    if ((fop->parent != NULL) || (fop->error != 0) || (loc->inode == NULL))
    {
        return;
    }

    LOCK(&loc->inode->lock);

    ctx = __ec_inode_get(loc->inode, fop->xl);
    if (ctx == NULL)
    {
        __ec_fop_set_error(fop, EIO);

        goto unlock;
    }

    if (ctx->inode_lock != NULL)
    {
        lock = ctx->inode_lock;
        ec_trace("LOCK_INODELK", fop, "lock=%p, inode=%p. Lock already "
                                      "acquired", lock, loc->inode);

        goto insert;
    }

    lock = ec_lock_allocate(fop->xl, EC_LOCK_INODE, loc);
    if (lock == NULL)
    {
        __ec_fop_set_error(fop, EIO);

        goto unlock;
    }

    ec_trace("LOCK_CREATE", fop, "lock=%p", lock);

    lock->flock.l_type = F_WRLCK;
    lock->flock.l_whence = SEEK_SET;

    lock->plock = &ctx->inode_lock;
    ctx->inode_lock = lock;

insert:
    link = ec_lock_insert(fop, lock, update);

unlock:
    UNLOCK(&loc->inode->lock);

    if (link != NULL) {
        ec_resume(link->fop, 0);
    }
}

void ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, int32_t update)
{
    loc_t loc;

    if ((fop->parent != NULL) || (fop->error != 0))
    {
        return;
    }

    if (ec_loc_from_fd(fop->xl, &loc, fd))
    {
        ec_lock_prepare_inode(fop, &loc, update);

        loc_wipe(&loc);
    }
    else
    {
        ec_fop_set_error(fop, EIO);
    }
}

int32_t ec_locked(call_frame_t * frame, void * cookie, xlator_t * this,
                  int32_t op_ret, int32_t op_errno, dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;
    ec_lock_t * lock = NULL;

    if (op_ret >= 0)
    {
        lock = fop->data;
        lock->mask = fop->good;
        lock->acquired = 1;

        fop->parent->mask &= fop->good;
        fop->parent->locked++;

        ec_trace("LOCKED", fop->parent, "lock=%p", lock);

        ec_lock(fop->parent);
    }
    else
    {
        gf_log(this->name, GF_LOG_WARNING, "Failed to complete preop lock");
    }

    return 0;
}

void ec_lock(ec_fop_data_t * fop)
{
    ec_lock_t * lock;

    while (fop->locked < fop->lock_count)
    {
        lock = fop->locks[fop->locked].lock;

        LOCK(&lock->loc.inode->lock);

        if (lock->owner != NULL)
        {
            ec_trace("LOCK_WAIT", fop, "lock=%p", lock);

            list_add_tail(&fop->locks[fop->locked].wait_list, &lock->waiting);

            fop->jobs++;
            fop->refs++;

            UNLOCK(&lock->loc.inode->lock);

            break;
        }
        lock->owner = fop;

        UNLOCK(&lock->loc.inode->lock);

        if (!lock->acquired)
        {
            ec_owner_set(fop->frame, lock);

            if (lock->kind == EC_LOCK_ENTRY)
            {
                ec_trace("LOCK_ACQUIRE", fop, "lock=%p, inode=%p, path=%s",
                         lock, lock->loc.inode, lock->loc.path);

                ec_entrylk(fop->frame, fop->xl, -1, EC_MINIMUM_ALL, ec_locked,
                           lock, fop->xl->name, &lock->loc, NULL,
                           ENTRYLK_LOCK, lock->type, NULL);
            }
            else
            {
                ec_trace("LOCK_ACQUIRE", fop, "lock=%p, inode=%p", lock,
                         lock->loc.inode);

                ec_inodelk(fop->frame, fop->xl, -1, EC_MINIMUM_ALL, ec_locked,
                           lock, fop->xl->name, &lock->loc, F_SETLKW,
                           &lock->flock, NULL);
            }

            break;
        }

        ec_trace("LOCK_REUSE", fop, "lock=%p", lock);

        if (lock->have_size)
        {
            fop->pre_size = fop->post_size = lock->size;
            fop->have_size = 1;
        }
        fop->mask &= lock->good_mask;

        fop->locked++;
    }
}

int32_t ec_get_size_version_set(call_frame_t * frame, void * cookie,
                                xlator_t * this, int32_t op_ret,
                                int32_t op_errno, inode_t * inode,
                                struct iatt * buf, dict_t * xdata,
                                struct iatt * postparent)
{
    ec_t * ec;
    ec_fop_data_t * fop = cookie;
    ec_inode_t * ctx;
    ec_lock_t *lock = NULL;

    if (op_ret >= 0)
    {
        if (buf->ia_type == IA_IFREG)
        {
            if (ec_dict_del_config(xdata, EC_XATTR_CONFIG, &fop->config) < 0)
            {
                gf_log(this->name, GF_LOG_ERROR, "Failed to get a valid "
                                                 "config");

                ec_fop_set_error(fop, EIO);

                return 0;
            }
            ec = this->private;
            if ((fop->config.version != EC_CONFIG_VERSION) ||
                (fop->config.algorithm != EC_CONFIG_ALGORITHM) ||
                (fop->config.gf_word_size != EC_GF_BITS) ||
                (fop->config.bricks != ec->nodes) ||
                (fop->config.redundancy != ec->redundancy) ||
                (fop->config.chunk_size != EC_METHOD_CHUNK_SIZE))
            {
                uint32_t data_bricks;

                // This combination of version/algorithm requires the following
                // values. Incorrect values for these fields are a sign of
                // corruption:
                //
                //   redundancy > 0
                //   redundancy * 2 < bricks
                //   gf_word_size must be a power of 2
                //   chunk_size (in bits) must be a multiple of gf_word_size *
                //       (bricks - redundancy)

                data_bricks = fop->config.bricks - fop->config.redundancy;
                if ((fop->config.redundancy < 1) ||
                    (fop->config.redundancy * 2 >= fop->config.bricks) ||
                    !ec_is_power_of_2(fop->config.gf_word_size) ||
                    ((fop->config.chunk_size * 8) % (fop->config.gf_word_size *
                                                     data_bricks) != 0))
                {
                    gf_log(this->name, GF_LOG_ERROR, "Invalid or corrupted "
                                                     "config (V=%u, A=%u, "
                                                     "W=%u, N=%u, R=%u, S=%u)",
                           fop->config.version, fop->config.algorithm,
                           fop->config.gf_word_size, fop->config.bricks,
                           fop->config.redundancy, fop->config.chunk_size);
                }
                else
                {
                    gf_log(this->name, GF_LOG_ERROR, "Unsupported config "
                                                     "(V=%u, A=%u, W=%u, "
                                                     "N=%u, R=%u, S=%u)",
                           fop->config.version, fop->config.algorithm,
                           fop->config.gf_word_size, fop->config.bricks,
                           fop->config.redundancy, fop->config.chunk_size);
                }

                ec_fop_set_error(fop, EIO);

                return 0;
            }
        }

        LOCK(&inode->lock);

        ctx = __ec_inode_get(inode, this);
        if (ctx != NULL) {
            if (ctx->inode_lock != NULL) {
                lock = ctx->inode_lock;
                lock->version = fop->answer->version;

                if (buf->ia_type == IA_IFREG) {
                    lock->have_size = 1;
                    lock->size = buf->ia_size;
                }
            }
            if (ctx->entry_lock != NULL) {
                lock = ctx->entry_lock;
                lock->version = fop->answer->version;
            }
        }

        UNLOCK(&inode->lock);

        if (lock != NULL)
        {
            // Only update parent mask if the lookup has been made with
            // inode locked.
            fop->parent->mask &= fop->good;
        }

        if (buf->ia_type == IA_IFREG) {
            fop->parent->pre_size = fop->parent->post_size = buf->ia_size;
            fop->parent->have_size = 1;
        }
    }
    else
    {
        gf_log(this->name, GF_LOG_WARNING, "Failed to get size and version "
                                           "(error %d)", op_errno);
        ec_fop_set_error(fop, op_errno);
    }

    return 0;
}

void ec_get_size_version(ec_fop_data_t * fop)
{
    loc_t loc;
    dict_t * xdata;
    uid_t uid;
    gid_t gid;
    int32_t error = ENOMEM;

    if (fop->have_size)
    {
        return;
    }

    if ((fop->parent != NULL) && fop->parent->have_size)
    {
        fop->pre_size = fop->parent->pre_size;
        fop->post_size = fop->parent->post_size;

        fop->have_size = 1;

        return;
    }

    memset(&loc, 0, sizeof(loc));

    xdata = dict_new();
    if (xdata == NULL)
    {
        goto out;
    }
    if ((dict_set_uint64(xdata, EC_XATTR_VERSION, 0) != 0) ||
        (dict_set_uint64(xdata, EC_XATTR_SIZE, 0) != 0) ||
        (dict_set_uint64(xdata, EC_XATTR_CONFIG, 0) != 0))
    {
        goto out;
    }

    uid = fop->frame->root->uid;
    gid = fop->frame->root->gid;

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    error = EIO;

    if (!fop->use_fd)
    {
        if (!ec_loc_from_loc(fop->xl, &loc, &fop->loc[0]))
        {
            goto out;
        }
        if (uuid_is_null(loc.pargfid))
        {
            if (loc.parent != NULL)
            {
                inode_unref(loc.parent);
                loc.parent = NULL;
            }
            GF_FREE((char *)loc.path);
            loc.path = NULL;
            loc.name = NULL;
        }
    }
    else if (!ec_loc_from_fd(fop->xl, &loc, fop->fd))
    {
        goto out;
    }

    /* For normal fops, ec_lookup() must succeed on at least EC_MINIMUM_MIN
     * bricks, however when this is called as part of a self-heal operation
     * the mask of target bricks (fop->mask) could contain less than
     * EC_MINIMUM_MIN bricks, causing the lookup to always fail. Thus we
     * always use the same minimum used for the main fop.
     */
    ec_lookup(fop->frame, fop->xl, fop->mask, fop->minimum,
              ec_get_size_version_set, NULL, &loc, xdata);

    fop->frame->root->uid = uid;
    fop->frame->root->gid = gid;

    error = 0;

out:
    loc_wipe(&loc);

    if (xdata != NULL)
    {
        dict_unref(xdata);
    }

    ec_fop_set_error(fop, error);
}

int32_t ec_unlocked(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    ec_fop_data_t *fop = cookie;

    if (op_ret < 0) {
        gf_log(this->name, GF_LOG_WARNING, "entry/inode unlocking failed (%s)",
               ec_fop_name(fop->parent->id));
    } else {
        ec_trace("UNLOCKED", fop->parent, "lock=%p", fop->data);
    }

    return 0;
}

void ec_unlock_lock(ec_fop_data_t *fop, ec_lock_t *lock)
{
    if (lock->mask != 0) {
        ec_owner_set(fop->frame, lock);

        switch (lock->kind) {
        case EC_LOCK_ENTRY:
            ec_trace("UNLOCK_ENTRYLK", fop, "lock=%p, inode=%p, path=%s", lock,
                     lock->loc.inode, lock->loc.path);

            ec_entrylk(fop->frame, fop->xl, lock->mask, EC_MINIMUM_ALL,
                       ec_unlocked, lock, fop->xl->name, &lock->loc, NULL,
                       ENTRYLK_UNLOCK, lock->type, NULL);

            break;

        case EC_LOCK_INODE:
            lock->flock.l_type = F_UNLCK;
            ec_trace("UNLOCK_INODELK", fop, "lock=%p, inode=%p", lock,
                     lock->loc.inode);

            ec_inodelk(fop->frame, fop->xl, lock->mask, EC_MINIMUM_ALL,
                       ec_unlocked, lock, fop->xl->name, &lock->loc, F_SETLK,
                       &lock->flock, NULL);

            break;

        default:
            gf_log(fop->xl->name, GF_LOG_ERROR, "Invalid lock type");
        }
    }

    ec_trace("LOCK_DESTROY", fop, "lock=%p", lock);

    ec_lock_destroy(lock);
}

int32_t ec_update_size_version_done(call_frame_t * frame, void * cookie,
                                    xlator_t * this, int32_t op_ret,
                                    int32_t op_errno, dict_t * xattr,
                                    dict_t * xdata)
{
    ec_fop_data_t * fop = cookie;

    if (op_ret < 0)
    {
        gf_log(fop->xl->name, GF_LOG_ERROR, "Failed to update version and "
                                            "size (error %d)", op_errno);
    }
    else
    {
        fop->parent->mask &= fop->good;
    }

    if (fop->data != NULL) {
        ec_unlock_lock(fop->parent, fop->data);
    }

    return 0;
}

void ec_update_size_version(ec_fop_data_t *fop, loc_t *loc, uint64_t version,
                            uint64_t size, ec_lock_t *lock)
{
    dict_t * dict;
    uid_t uid;
    gid_t gid;

    if (fop->parent != NULL)
    {
        fop->parent->post_size = fop->post_size;

        return;
    }

    ec_trace("UPDATE", fop, "version=%ld, size=%ld", version, size);

    dict = dict_new();
    if (dict == NULL)
    {
        goto out;
    }

    if (ec_dict_set_number(dict, EC_XATTR_VERSION, version) != 0)
    {
        goto out;
    }
    if (size != 0)
    {
        if (ec_dict_set_number(dict, EC_XATTR_SIZE, size) != 0)
        {
            goto out;
        }
    }

    uid = fop->frame->root->uid;
    gid = fop->frame->root->gid;

    fop->frame->root->uid = 0;
    fop->frame->root->gid = 0;

    ec_xattrop(fop->frame, fop->xl, fop->mask, EC_MINIMUM_MIN,
               ec_update_size_version_done, lock, loc,
               GF_XATTROP_ADD_ARRAY64, dict, NULL);

    fop->frame->root->uid = uid;
    fop->frame->root->gid = gid;

    dict_unref(dict);

    return;

out:
    if (dict != NULL)
    {
        dict_unref(dict);
    }

    ec_fop_set_error(fop, EIO);

    gf_log(fop->xl->name, GF_LOG_ERROR, "Unable to update version and size");
}

void ec_unlock_now(ec_fop_data_t *fop, ec_lock_t *lock)
{
    ec_trace("UNLOCK_NOW", fop, "lock=%p", lock);

    if (lock->version_delta != 0) {
        ec_update_size_version(fop, &lock->loc, lock->version_delta,
                               lock->size_delta, lock);
    } else {
        ec_unlock_lock(fop, lock);
    }

    ec_resume(fop, 0);
}

void ec_unlock_timer_cbk(void *data)
{
    ec_lock_link_t *link = data;
    ec_lock_t *lock = link->lock;
    ec_fop_data_t *fop = NULL;

    LOCK(&lock->loc.inode->lock);

    if (lock->timer != NULL) {
        fop = link->fop;

        ec_trace("UNLOCK_DELAYED", fop, "lock=%p", lock);

        GF_ASSERT(lock->refs == 1);

        gf_timer_call_cancel(fop->xl->ctx, lock->timer);
        lock->timer = NULL;
        *lock->plock = NULL;
    }

    UNLOCK(&lock->loc.inode->lock);

    if (fop != NULL) {
        ec_unlock_now(fop, lock);
    }
}

void ec_unlock_timer_add(ec_lock_link_t *link)
{
    struct timespec delay;
    ec_fop_data_t *fop = link->fop;
    ec_lock_t *lock = link->lock;
    int32_t refs = 1;

    LOCK(&lock->loc.inode->lock);

    GF_ASSERT(lock->timer == NULL);

    if (lock->refs != 1) {
        ec_trace("UNLOCK_SKIP", fop, "lock=%p", lock);

        lock->refs--;

        UNLOCK(&lock->loc.inode->lock);
    } else {
        ec_trace("UNLOCK_DELAY", fop, "lock=%p", lock);

        delay.tv_sec = 1;
        delay.tv_nsec = 0;

        LOCK(&fop->lock);

        fop->jobs++;
        fop->refs++;

        UNLOCK(&fop->lock);

        lock->timer = gf_timer_call_after(fop->xl->ctx, delay,
                                          ec_unlock_timer_cbk, link);
        if (lock->timer == NULL) {
            gf_log(fop->xl->name, GF_LOG_WARNING, "Unable to delay an unlock");

            *lock->plock = NULL;
            refs = 0;
        }

        UNLOCK(&lock->loc.inode->lock);

        if (refs == 0) {
            ec_unlock_now(fop, lock);
        }
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
    ec_lock_t * lock;
    uint64_t version, delta;

    GF_ASSERT(fop->lock_count == 1);

    lock = fop->locks[0].lock;

    LOCK(&lock->loc.inode->lock);

    GF_ASSERT(lock->owner == fop);

    version = lock->version_delta;
    delta = lock->size_delta;
    lock->version_delta = 0;
    lock->size_delta = 0;

    UNLOCK(&lock->loc.inode->lock);

    if (version > 0)
    {
        ec_update_size_version(fop, &lock->loc, version, delta, NULL);
    }
}

void ec_lock_reuse(ec_fop_data_t *fop)
{
    ec_fop_data_t * wait_fop;
    ec_lock_t * lock;
    ec_lock_link_t * link;
    int32_t i;

    for (i = 0; i < fop->lock_count; i++)
    {
        wait_fop = NULL;

        lock = fop->locks[i].lock;

        LOCK(&lock->loc.inode->lock);

        ec_trace("LOCK_DONE", fop, "lock=%p", lock);

        GF_ASSERT(lock->owner == fop);
        lock->owner = NULL;

        if (((fop->locks_update >> i) & 1) != 0) {
            if (fop->error == 0)
            {
                lock->version_delta++;
                lock->size_delta += fop->post_size - fop->pre_size;
                if (fop->have_size) {
                    lock->size = fop->post_size;
                    lock->have_size = 1;
                }
            }
        }

        lock->good_mask &= fop->mask;

        if (!list_empty(&lock->waiting))
        {
            link = list_entry(lock->waiting.next, ec_lock_link_t, wait_list);
            list_del_init(&link->wait_list);

            wait_fop = link->fop;

            if (lock->kind == EC_LOCK_INODE)
            {
                wait_fop->pre_size = wait_fop->post_size = fop->post_size;
                wait_fop->have_size = fop->have_size;
            }
            wait_fop->mask &= fop->mask;
        }

        UNLOCK(&lock->loc.inode->lock);

        if (wait_fop != NULL)
        {
            ec_lock(wait_fop);

            ec_resume(wait_fop, 0);
        }
    }
}

void __ec_manager(ec_fop_data_t * fop, int32_t error)
{
    do
    {
        ec_trace("MANAGER", fop, "error=%d", error);

        if (fop->state == EC_STATE_END)
        {
            ec_fop_data_release(fop);

            break;
        }

        if (error != 0)
        {
            fop->error = error;
            fop->state = -fop->state;
        }

        fop->state = fop->handler(fop, fop->state);

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
