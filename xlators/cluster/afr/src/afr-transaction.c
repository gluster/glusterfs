/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/dict.h>
#include <glusterfs/timer.h>

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-messages.h"

#include <signal.h>

typedef enum {
    AFR_TRANSACTION_PRE_OP,
    AFR_TRANSACTION_POST_OP,
} afr_xattrop_type_t;

static void
afr_lock_resume_shared(struct list_head *list);

static void
afr_post_op_handle_success(call_frame_t *frame, xlator_t *this);

static void
afr_post_op_handle_failure(call_frame_t *frame, xlator_t *this, int op_errno);

static int
afr_internal_lock_finish(call_frame_t *frame, xlator_t *this);

static void
__afr_transaction_wake_shared(afr_local_t *local, struct list_head *shared);

static void
afr_changelog_post_op_do(call_frame_t *frame, xlator_t *this);

static int
afr_changelog_post_op_safe(call_frame_t *frame, xlator_t *this);

static gf_boolean_t
afr_changelog_pre_op_uninherit(call_frame_t *frame, xlator_t *this);

static gf_boolean_t
afr_changelog_pre_op_update(call_frame_t *frame, xlator_t *this);

static int
afr_changelog_call_count(afr_transaction_type type,
                         unsigned char *pre_op_subvols,
                         unsigned char *failed_subvols,
                         unsigned int child_count);
static int
afr_changelog_do(call_frame_t *frame, xlator_t *this, dict_t *xattr,
                 afr_changelog_resume_t changelog_resume,
                 afr_xattrop_type_t op);

static void
afr_ta_decide_post_op_state(call_frame_t *frame, xlator_t *this);

static int
afr_ta_post_op_do(void *opaque);

static int
afr_ta_post_op_synctask(xlator_t *this, afr_local_t *local);

static int
afr_changelog_post_op_done(call_frame_t *frame, xlator_t *this);

static void
afr_changelog_post_op_fail(call_frame_t *frame, xlator_t *this, int op_errno);

void
afr_ta_locked_priv_invalidate(afr_private_t *priv)
{
    priv->ta_bad_child_index = AFR_CHILD_UNKNOWN;
    priv->release_ta_notify_dom_lock = _gf_false;
    priv->ta_notify_dom_lock_offset = 0;
}

static void
afr_ta_process_waitq(xlator_t *this)
{
    afr_local_t *entry = NULL;
    afr_private_t *priv = this->private;
    struct list_head waitq = {
        0,
    };

    INIT_LIST_HEAD(&waitq);
    LOCK(&priv->lock);
    list_splice_init(&priv->ta_waitq, &waitq);
    UNLOCK(&priv->lock);
    list_for_each_entry(entry, &waitq, ta_waitq)
    {
        afr_ta_decide_post_op_state(entry->transaction.frame, this);
    }
}

int
afr_ta_lock_release_done(int ret, call_frame_t *ta_frame, void *opaque)
{
    afr_ta_process_waitq(ta_frame->this);
    STACK_DESTROY(ta_frame->root);
    return 0;
}

int
afr_release_notify_lock_for_ta(void *opaque)
{
    xlator_t *this = NULL;
    afr_private_t *priv = NULL;
    loc_t loc = {
        0,
    };
    struct gf_flock flock = {
        0,
    };
    int ret = -1;

    this = (xlator_t *)opaque;
    priv = this->private;
    ret = afr_fill_ta_loc(this, &loc, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate loc for thin-arbiter.");
        goto out;
    }
    flock.l_type = F_UNLCK;
    flock.l_start = priv->ta_notify_dom_lock_offset;
    flock.l_len = 1;
    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_NOTIFY, &loc, F_SETLK, &flock, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to unlock AFR_TA_DOM_NOTIFY lock.");
    }

    LOCK(&priv->lock);
    {
        afr_ta_locked_priv_invalidate(priv);
    }
    UNLOCK(&priv->lock);
out:
    loc_wipe(&loc);
    return ret;
}

static void
gf_zero_fill_stat(struct iatt *buf)
{
    buf->ia_nlink = 0;
    buf->ia_ctime = 0;
}

void
afr_zero_fill_stat(afr_local_t *local)
{
    if (!local)
        return;
    if (local->transaction.type == AFR_DATA_TRANSACTION ||
        local->transaction.type == AFR_METADATA_TRANSACTION) {
        gf_zero_fill_stat(&local->cont.inode_wfop.prebuf);
        gf_zero_fill_stat(&local->cont.inode_wfop.postbuf);
    } else if (local->transaction.type == AFR_ENTRY_TRANSACTION ||
               local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) {
        gf_zero_fill_stat(&local->cont.dir_fop.buf);
        gf_zero_fill_stat(&local->cont.dir_fop.preparent);
        gf_zero_fill_stat(&local->cont.dir_fop.postparent);
        if (local->transaction.type == AFR_ENTRY_TRANSACTION)
            return;
        gf_zero_fill_stat(&local->cont.dir_fop.prenewparent);
        gf_zero_fill_stat(&local->cont.dir_fop.postnewparent);
    }
}

/* In case of errors afr needs to choose which xdata from lower xlators it needs
 * to unwind with. The way it is done is by checking if there are
 * any good subvols which failed. Give preference to errnos other than
 * ENOTCONN even if the child is source */
void
afr_pick_error_xdata(afr_local_t *local, afr_private_t *priv, inode_t *inode1,
                     unsigned char *readable1, inode_t *inode2,
                     unsigned char *readable2)
{
    int s = -1; /*selection*/
    int i = 0;
    unsigned char *readable = NULL;

    if (local->xdata_rsp) {
        dict_unref(local->xdata_rsp);
        local->xdata_rsp = NULL;
    }

    readable = alloca0(priv->child_count * sizeof(*readable));
    if (inode2 && readable2) { /*rename fop*/
        AFR_INTERSECT(readable, readable1, readable2, priv->child_count);
    } else {
        memcpy(readable, readable1, sizeof(*readable) * priv->child_count);
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;

        if (local->replies[i].op_ret >= 0)
            continue;

        if (local->replies[i].op_errno == ENOTCONN)
            continue;

        /*Order is important in the following condition*/
        if ((s < 0) || (!readable[s] && readable[i]))
            s = i;
    }

    if (s != -1 && local->replies[s].xdata) {
        local->xdata_rsp = dict_ref(local->replies[s].xdata);
    } else if (s == -1) {
        for (i = 0; i < priv->child_count; i++) {
            if (!local->replies[i].valid)
                continue;

            if (local->replies[i].op_ret >= 0)
                continue;

            if (!local->replies[i].xdata)
                continue;
            local->xdata_rsp = dict_ref(local->replies[i].xdata);
            break;
        }
    }
}

gf_boolean_t
afr_needs_changelog_update(afr_local_t *local)
{
    if (local->transaction.type == AFR_DATA_TRANSACTION)
        return _gf_true;
    if (!local->optimistic_change_log)
        return _gf_true;
    return _gf_false;
}

static gf_boolean_t
afr_changelog_has_quorum(afr_local_t *local, xlator_t *this)
{
    afr_private_t *priv = NULL;
    int i = 0;
    unsigned char *success_children = NULL;

    priv = this->private;
    success_children = alloca0(priv->child_count);

    for (i = 0; i < priv->child_count; i++) {
        if (!local->transaction.failed_subvols[i]) {
            success_children[i] = 1;
        }
    }

    if (afr_has_quorum(success_children, this, NULL)) {
        return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
afr_is_write_subvol_valid(call_frame_t *frame, xlator_t *this)
{
    int i = 0;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    uint64_t write_subvol = 0;
    unsigned char *writable = NULL;
    uint16_t datamap = 0;

    local = frame->local;
    priv = this->private;
    writable = alloca0(priv->child_count);

    write_subvol = afr_write_subvol_get(frame, this);
    datamap = (write_subvol & 0x00000000ffff0000) >> 16;
    for (i = 0; i < priv->child_count; i++) {
        if (datamap & (1 << i))
            writable[i] = 1;

        if (writable[i] && !local->transaction.failed_subvols[i])
            return _gf_true;
    }

    return _gf_false;
}

int
afr_transaction_fop(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = -1;
    unsigned char *failed_subvols = NULL;
    int i = 0;

    local = frame->local;
    priv = this->private;

    failed_subvols = local->transaction.failed_subvols;
    call_count = priv->child_count -
                 AFR_COUNT(failed_subvols, priv->child_count);
    /* Fail if pre-op did not succeed on quorum no. of bricks. */
    if (!afr_changelog_has_quorum(local, this) || !call_count) {
        local->op_ret = -1;
        /* local->op_errno is already captured in changelog cbk. */
        afr_transaction_resume(frame, this);
        return 0;
    }

    /* Fail if at least one writeable brick isn't up.*/
    if (local->transaction.type == AFR_DATA_TRANSACTION &&
        !afr_is_write_subvol_valid(frame, this)) {
        local->op_ret = -1;
        local->op_errno = EIO;
        afr_transaction_resume(frame, this);
        return 0;
    }

    local->call_count = call_count;
    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.pre_op[i] && !failed_subvols[i]) {
            local->transaction.wind(frame, this, i);

            if (!--call_count)
                break;
        }
    }

    return 0;
}

static int
afr_transaction_done(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    gf_boolean_t unwind = _gf_false;
    afr_lock_t *lock = NULL;
    afr_local_t *lock_local = NULL;

    priv = this->private;
    local = frame->local;

    if (priv->consistent_metadata) {
        LOCK(&frame->lock);
        {
            unwind = (local->transaction.main_frame != NULL);
        }
        UNLOCK(&frame->lock);
        if (unwind) /*It definitely did post-op*/
            afr_zero_fill_stat(local);
    }

    if (local->transaction.do_eager_unlock) {
        lock = &local->inode_ctx->lock[local->transaction.type];
        LOCK(&local->inode->lock);
        {
            lock->acquired = _gf_false;
            lock->release = _gf_false;
            list_splice_init(&lock->frozen, &lock->waiting);
            if (list_empty(&lock->waiting))
                goto unlock;
            lock_local = list_entry(lock->waiting.next, afr_local_t,
                                    transaction.wait_list);
            list_del_init(&lock_local->transaction.wait_list);
            list_add(&lock_local->transaction.owner_list, &lock->owners);
        }
    unlock:
        UNLOCK(&local->inode->lock);
    }
    if (lock_local) {
        afr_lock(lock_local->transaction.frame,
                 lock_local->transaction.frame->this);
    }
    local->transaction.unwind(frame, this);

    GF_ASSERT(list_empty(&local->transaction.owner_list));
    GF_ASSERT(list_empty(&local->transaction.wait_list));
    AFR_STACK_DESTROY(frame);

    return 0;
}

static void
afr_lock_fail_shared(afr_local_t *local, struct list_head *list)
{
    afr_local_t *each = NULL;

    while (!list_empty(list)) {
        each = list_entry(list->next, afr_local_t, transaction.wait_list);
        list_del_init(&each->transaction.wait_list);
        each->op_ret = -1;
        each->op_errno = local->op_errno;
        afr_transaction_done(each->transaction.frame,
                             each->transaction.frame->this);
    }
}

static void
afr_handle_lock_acquire_failure(afr_local_t *local)
{
    struct list_head shared;
    afr_lock_t *lock = NULL;

    if (!local->transaction.eager_lock_on)
        goto out;

    lock = &local->inode_ctx->lock[local->transaction.type];

    INIT_LIST_HEAD(&shared);
    LOCK(&local->inode->lock);
    {
        lock->release = _gf_true;
        list_splice_init(&lock->waiting, &shared);
    }
    UNLOCK(&local->inode->lock);

    afr_lock_fail_shared(local, &shared);
    local->transaction.do_eager_unlock = _gf_true;
out:
    local->internal_lock.lock_cbk = afr_transaction_done;
    afr_unlock(local->transaction.frame, local->transaction.frame->this);
}

call_frame_t *
afr_transaction_detach_fop_frame(call_frame_t *frame)
{
    afr_local_t *local = NULL;
    call_frame_t *fop_frame = NULL;

    local = frame->local;

    afr_handle_inconsistent_fop(frame, &local->op_ret, &local->op_errno);
    LOCK(&frame->lock);
    {
        fop_frame = local->transaction.main_frame;
        local->transaction.main_frame = NULL;
    }
    UNLOCK(&frame->lock);

    return fop_frame;
}

static void
afr_save_lk_owner(call_frame_t *frame)
{
    afr_local_t *local = NULL;

    local = frame->local;

    lk_owner_copy(&local->saved_lk_owner, &frame->root->lk_owner);
}

static void
afr_restore_lk_owner(call_frame_t *frame)
{
    afr_local_t *local = NULL;

    local = frame->local;

    lk_owner_copy(&frame->root->lk_owner, &local->saved_lk_owner);
}

void
__mark_all_success(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        local->transaction.failed_subvols[i] = 0;
    }
}

static void
afr_compute_pre_op_sources(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_transaction_type type = -1;
    dict_t *xdata = NULL;
    int **matrix = NULL;
    int idx = -1;
    int i = 0;
    int j = 0;

    priv = this->private;
    local = frame->local;
    type = local->transaction.type;
    idx = afr_index_for_transaction_type(type);
    matrix = ALLOC_MATRIX(priv->child_count, int);

    for (i = 0; i < priv->child_count; i++) {
        if (!local->transaction.changelog_xdata[i])
            continue;
        xdata = local->transaction.changelog_xdata[i];
        afr_selfheal_fill_matrix(this, matrix, i, idx, xdata);
    }

    memset(local->transaction.pre_op_sources, 1, priv->child_count);

    /*If lock or pre-op failed on a brick, it is not a source. */
    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.failed_subvols[i])
            local->transaction.pre_op_sources[i] = 0;
    }

    /* If brick is blamed by others, it is not a source. */
    for (i = 0; i < priv->child_count; i++)
        for (j = 0; j < priv->child_count; j++)
            if (matrix[i][j] != 0)
                local->transaction.pre_op_sources[j] = 0;
}

static void
afr_txn_arbitrate_fop(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int pre_op_sources_count = 0;
    int i = 0;

    priv = this->private;
    local = frame->local;

    afr_compute_pre_op_sources(frame, this);
    pre_op_sources_count = AFR_COUNT(local->transaction.pre_op_sources,
                                     priv->child_count);

    /* If arbiter is the only source, do not proceed. */
    if (pre_op_sources_count < 2 &&
        local->transaction.pre_op_sources[ARBITER_BRICK_INDEX]) {
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        for (i = 0; i < priv->child_count; i++)
            local->transaction.failed_subvols[i] = 1;
    }

    afr_transaction_fop(frame, this);

    return;
}

static int
afr_transaction_perform_fop(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int ret = 0;
    int failure_count = 0;
    struct list_head shared;
    afr_lock_t *lock = NULL;

    local = frame->local;
    priv = this->private;

    INIT_LIST_HEAD(&shared);
    if (local->transaction.type == AFR_DATA_TRANSACTION &&
        !local->transaction.inherited) {
        ret = afr_write_subvol_set(frame, this);
        if (ret) {
            /*act as if operation failed on all subvols*/
            local->op_ret = -1;
            local->op_errno = -ret;
            for (i = 0; i < priv->child_count; i++)
                local->transaction.failed_subvols[i] = 1;
        }
    }

    if (local->pre_op_compat)
        /* old mode, pre-op was done as afr_changelog_do()
           just now, before OP */
        afr_changelog_pre_op_update(frame, this);

    if (!local->transaction.eager_lock_on || local->transaction.inherited)
        goto fop;
    failure_count = AFR_COUNT(local->transaction.failed_subvols,
                              priv->child_count);
    if (failure_count == priv->child_count) {
        afr_handle_lock_acquire_failure(local);
        return 0;
    } else {
        lock = &local->inode_ctx->lock[local->transaction.type];
        LOCK(&local->inode->lock);
        {
            lock->acquired = _gf_true;
            __afr_transaction_wake_shared(local, &shared);
        }
        UNLOCK(&local->inode->lock);
    }

fop:
    /*  Perform fops with the lk-owner from top xlator.
     *  Eg: lk-owner of posix-lk and flush should be same,
     *  flush cant clear the  posix-lks without that lk-owner.
     */
    afr_save_lk_owner(frame);
    lk_owner_copy(&frame->root->lk_owner,
                  &local->transaction.main_frame->root->lk_owner);

    if (priv->arbiter_count == 1) {
        afr_txn_arbitrate_fop(frame, this);
    } else {
        afr_transaction_fop(frame, this);
    }

    afr_lock_resume_shared(&shared);
    return 0;
}

int
afr_set_pending_dict(afr_private_t *priv, dict_t *xattr, int **pending)
{
    int i = 0;
    int ret = 0;

    for (i = 0; i < priv->child_count; i++) {
        ret = dict_set_static_bin(xattr, priv->pending_key[i], pending[i],
                                  AFR_NUM_CHANGE_LOGS * sizeof(int));
        /* 3 = data+metadata+entry */

        if (ret)
            break;
    }

    return ret;
}

static void
afr_ta_dom_lock_check_and_release(afr_ta_fop_state_t fop_state, xlator_t *this)
{
    afr_private_t *priv = this->private;
    unsigned int inmem_count = 0;
    unsigned int onwire_count = 0;
    gf_boolean_t release = _gf_false;

    LOCK(&priv->lock);
    {
        /*Once we get notify lock release upcall notification,
         if any of the fop state counters are non-zero, we will
         not release the lock.
         */
        onwire_count = priv->ta_on_wire_txn_count;
        inmem_count = priv->ta_in_mem_txn_count;
        switch (fop_state) {
            case TA_GET_INFO_FROM_TA_FILE:
                onwire_count = --priv->ta_on_wire_txn_count;
                break;
            case TA_INFO_IN_MEMORY_SUCCESS:
            case TA_INFO_IN_MEMORY_FAILED:
                inmem_count = --priv->ta_in_mem_txn_count;
                break;
            case TA_WAIT_FOR_NOTIFY_LOCK_REL:
                GF_ASSERT(0);
                break;
            case TA_SUCCESS:
                break;
        }
        release = priv->release_ta_notify_dom_lock;
    }
    UNLOCK(&priv->lock);

    if (inmem_count != 0 || release == _gf_false || onwire_count != 0)
        return;

    afr_ta_lock_release_synctask(this);
}

static void
afr_ta_process_onwireq(afr_ta_fop_state_t fop_state, xlator_t *this)
{
    afr_private_t *priv = this->private;
    afr_local_t *entry = NULL;
    int bad_child = AFR_CHILD_UNKNOWN;

    struct list_head onwireq = {
        0,
    };
    INIT_LIST_HEAD(&onwireq);

    LOCK(&priv->lock);
    {
        bad_child = priv->ta_bad_child_index;
        if (bad_child == AFR_CHILD_UNKNOWN) {
            /*The previous on-wire ta_post_op was a failure. Just dequeue
             *one element to wind on-wire again. */
            entry = list_entry(priv->ta_onwireq.next, afr_local_t, ta_onwireq);
            list_del_init(&entry->ta_onwireq);
        } else {
            /* Prepare to process all fops based on bad_child_index. */
            list_splice_init(&priv->ta_onwireq, &onwireq);
        }
    }
    UNLOCK(&priv->lock);

    if (entry) {
        afr_ta_post_op_synctask(this, entry);
        return;
    } else {
        while (!list_empty(&onwireq)) {
            entry = list_entry(onwireq.next, afr_local_t, ta_onwireq);
            list_del_init(&entry->ta_onwireq);
            if (entry->ta_failed_subvol == bad_child) {
                afr_post_op_handle_success(entry->transaction.frame, this);
            } else {
                afr_post_op_handle_failure(entry->transaction.frame, this, EIO);
            }
        }
    }
}

static int
afr_changelog_post_op_done(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_internal_lock_t *int_lock = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;
    int_lock = &local->internal_lock;

    if (priv->thin_arbiter_count) {
        /*fop should not come here with TA_WAIT_FOR_NOTIFY_LOCK_REL state */
        afr_ta_dom_lock_check_and_release(local->fop_state, this);
    }

    /* Fail the FOP if post-op did not succeed on quorum no. of bricks. */
    if (!afr_changelog_has_quorum(local, this)) {
        local->op_ret = -1;
        /*local->op_errno is already captured in changelog cbk*/
    }

    if (local->transaction.resume_stub) {
        call_resume(local->transaction.resume_stub);
        local->transaction.resume_stub = NULL;
    }

    int_lock->lock_cbk = afr_transaction_done;
    afr_unlock(frame, this);

    return 0;
}

static void
afr_changelog_post_op_fail(call_frame_t *frame, xlator_t *this, int op_errno)
{
    afr_local_t *local = frame->local;
    local->op_ret = -1;
    local->op_errno = op_errno;

    gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_THIN_ARB,
           "Failing %s for gfid %s. Fop state is:%d", gf_fop_list[local->op],
           uuid_utoa(local->inode->gfid), local->fop_state);

    afr_changelog_post_op_done(frame, this);
}

unsigned char *
afr_locked_nodes_get(afr_transaction_type type, afr_internal_lock_t *int_lock)
{
    /*Because same set of subvols participate in all lockee
     * entities*/
    return int_lock->lockee[0].locked_nodes;
}

static int
afr_changelog_call_count(afr_transaction_type type,
                         unsigned char *pre_op_subvols,
                         unsigned char *failed_subvols,
                         unsigned int child_count)
{
    int i = 0;
    int call_count = 0;

    for (i = 0; i < child_count; i++) {
        if (pre_op_subvols[i] && !failed_subvols[i]) {
            call_count++;
        }
    }

    if (type == AFR_ENTRY_RENAME_TRANSACTION)
        call_count *= 2;

    return call_count;
}

gf_boolean_t
afr_txn_nothing_failed(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;

    local = frame->local;
    priv = this->private;

    if (priv->thin_arbiter_count) {
        /* We need to perform post-op even if 1 data brick was down
         * before the txn started.*/
        if (AFR_COUNT(local->transaction.failed_subvols, priv->child_count))
            return _gf_false;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.pre_op[i] &&
            local->transaction.failed_subvols[i])
            return _gf_false;
    }

    return _gf_true;
}

static void
afr_handle_symmetric_errors(call_frame_t *frame, xlator_t *this)
{
    if (afr_is_symmetric_error(frame, this))
        __mark_all_success(frame, this);
}

gf_boolean_t
afr_has_quorum(unsigned char *subvols, xlator_t *this, call_frame_t *frame)
{
    unsigned int quorum_count = 0;
    afr_private_t *priv = NULL;
    unsigned int up_children_count = 0;

    priv = this->private;
    up_children_count = AFR_COUNT(subvols, priv->child_count);

    if (afr_lookup_has_quorum(frame, up_children_count))
        return _gf_true;

    if (priv->quorum_count == AFR_QUORUM_AUTO) {
        /*
         * Special case for auto-quorum with an even number of nodes.
         *
         * A replica set with even count N can only handle the same
         * number of failures as odd N-1 before losing "vanilla"
         * quorum, and the probability of more simultaneous failures is
         * actually higher.  For example, with a 1% chance of failure
         * we'd have a 0.03% chance of two simultaneous failures with
         * N=3 but a 0.06% chance with N=4.  However, the special case
         * is necessary for N=2 because there's no real quorum in that
         * case (i.e. can't normally survive *any* failures).  In that
         * case, we treat the first node as a tie-breaker, allowing
         * quorum to be retained in some cases while still honoring the
         * all-important constraint that there can not simultaneously
         * be two partitioned sets of nodes each believing they have
         * quorum.  Of two equally sized sets, the one without that
         * first node will lose.
         *
         * It turns out that the special case is beneficial for higher
         * values of N as well.  Continuing the example above, the
         * probability of losing quorum with N=4 and this type of
         * quorum is (very) slightly lower than with N=3 and vanilla
         * quorum.  The difference becomes even more pronounced with
         * higher N.  Therefore, even though such replica counts are
         * unlikely to be seen in practice, we might as well use the
         * "special" quorum then as well.
         */
        if ((up_children_count * 2) == priv->child_count) {
            return subvols[0];
        }
    }

    if (priv->quorum_count == AFR_QUORUM_AUTO) {
        quorum_count = priv->child_count / 2 + 1;
    } else {
        quorum_count = priv->quorum_count;
    }

    if (up_children_count >= quorum_count)
        return _gf_true;

    return _gf_false;
}

static gf_boolean_t
afr_has_fop_quorum(call_frame_t *frame)
{
    xlator_t *this = frame->this;
    afr_local_t *local = frame->local;
    unsigned char *locked_nodes = NULL;

    locked_nodes = afr_locked_nodes_get(local->transaction.type,
                                        &local->internal_lock);
    return afr_has_quorum(locked_nodes, this, NULL);
}

static gf_boolean_t
afr_has_fop_cbk_quorum(call_frame_t *frame)
{
    afr_local_t *local = frame->local;
    xlator_t *this = frame->this;
    afr_private_t *priv = this->private;
    unsigned char *success = alloca0(priv->child_count);
    int i = 0;

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.pre_op[i])
            if (!local->transaction.failed_subvols[i])
                success[i] = 1;
    }

    return afr_has_quorum(success, this, NULL);
}

static gf_boolean_t
afr_need_dirty_marking(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = this->private;
    afr_local_t *local = NULL;
    gf_boolean_t need_dirty = _gf_false;

    local = frame->local;

    if (!priv->quorum_count || !local->optimistic_change_log)
        return _gf_false;

    if (local->transaction.type == AFR_DATA_TRANSACTION ||
        local->transaction.type == AFR_METADATA_TRANSACTION)
        return _gf_false;

    if (AFR_COUNT(local->transaction.failed_subvols, priv->child_count) ==
        priv->child_count)
        return _gf_false;

    if (!afr_has_fop_cbk_quorum(frame))
        need_dirty = _gf_true;

    return need_dirty;
}

static void
afr_handle_quorum(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    const char *file = NULL;
    uuid_t gfid = {0};

    local = frame->local;
    priv = frame->this->private;

    if (priv->quorum_count == 0)
        return;

    /* If the fop already failed return right away to preserve errno */
    if (local->op_ret == -1)
        return;

    /*
     * Network split may happen just after the fops are unwound, so check
     * if the fop succeeded in a way it still follows quorum. If it doesn't,
     * mark the fop as failure, mark the changelogs so it reflects that
     * failure.
     *
     * Scenario:
     * There are 3 mounts on 3 machines(node1, node2, node3) all writing to
     * single file. Network split happened in a way that node1 can't see
     * node2, node3. Node2, node3 both of them can't see node1. Now at the
     * time of sending write all the bricks are up. Just after write fop is
     * wound on node1, network split happens. Node1 thinks write fop failed
     * on node2, node3 so marks pending changelog for those 2 extended
     * attributes on node1. Node2, node3 thinks writes failed on node1 so
     * they mark pending changelog for node1. When the network is stable
     * again the file already is in split-brain. These checks prevent
     * marking pending changelog on other subvolumes if the fop doesn't
     * succeed in a way it is still following quorum. So with this fix what
     * is happening is, node1 will have all pending changelog(FOOL) because
     * the write succeeded only on node1 but failed on node2, node3 so
     * instead of marking pending changelogs on node2, node3 it just treats
     * the fop as failure and goes into DIRTY state. Where as node2, node3
     * say they are sources and have pending changelog to node1 so there is
     * no split-brain with the fix. The problem is eliminated completely.
     */

    if (afr_has_fop_cbk_quorum(frame))
        return;

    if (afr_need_dirty_marking(frame, this))
        goto set_response;

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.pre_op[i])
            afr_transaction_fop_failed(frame, frame->this, i);
    }

set_response:
    local->op_ret = -1;
    local->op_errno = afr_final_errno(local, priv);
    if (local->op_errno == 0)
        local->op_errno = afr_quorum_errno(priv);

    if (local->fd) {
        gf_uuid_copy(gfid, local->fd->inode->gfid);
        file = uuid_utoa(gfid);
    } else {
        loc_path(&local->loc, local->loc.name);
        file = local->loc.path;
    }

    gf_msg(frame->this->name, GF_LOG_WARNING, local->op_errno,
           AFR_MSG_QUORUM_FAIL, "%s: Failing %s as quorum is not met", file,
           gf_fop_list[local->op]);

    switch (local->transaction.type) {
        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
            afr_pick_error_xdata(local, priv, local->parent, local->readable,
                                 local->parent2, local->readable2);
            break;
        default:
            afr_pick_error_xdata(local, priv, local->inode, local->readable,
                                 NULL, NULL);
            break;
    }
}

int
afr_fill_ta_loc(xlator_t *this, loc_t *loc, gf_boolean_t is_gfid_based_fop)
{
    afr_private_t *priv = NULL;

    priv = this->private;
    loc->parent = inode_ref(this->itable->root);
    gf_uuid_copy(loc->pargfid, loc->parent->gfid);
    loc->name = priv->pending_key[THIN_ARBITER_BRICK_INDEX];
    if (is_gfid_based_fop && gf_uuid_is_null(priv->ta_gfid)) {
        /* Except afr_ta_id_file_check() which is path based, all other gluster
         * FOPS need gfid.*/
        return -EINVAL;
    }
    gf_uuid_copy(loc->gfid, priv->ta_gfid);
    loc->inode = inode_new(loc->parent->table);
    if (!loc->inode) {
        loc_wipe(loc);
        return -ENOMEM;
    }
    return 0;
}

static int
afr_ta_post_op_done(int ret, call_frame_t *frame, void *opaque)
{
    xlator_t *this = NULL;
    afr_local_t *local = NULL;
    call_frame_t *txn_frame = NULL;
    afr_ta_fop_state_t fop_state;

    local = (afr_local_t *)opaque;
    fop_state = local->fop_state;
    txn_frame = local->transaction.frame;
    this = frame->this;

    if (ret == 0) {
        /*Mark pending xattrs on the up data brick.*/
        afr_post_op_handle_success(txn_frame, this);
    } else {
        afr_post_op_handle_failure(txn_frame, this, -ret);
    }

    STACK_DESTROY(frame->root);
    afr_ta_process_onwireq(fop_state, this);

    return 0;
}

static int **
afr_set_changelog_xattr(afr_private_t *priv, unsigned char *pending,
                        dict_t *xattr, afr_local_t *local)
{
    int **changelog = NULL;
    int idx = 0;
    int ret = 0;
    int i;
    uint32_t hton32_1;

    if (local->is_new_entry == _gf_true) {
        changelog = afr_mark_pending_changelog(priv, pending, xattr,
                                               local->cont.dir_fop.buf.ia_type);
    } else {
        idx = afr_index_for_transaction_type(local->transaction.type);
        changelog = afr_matrix_create(priv->child_count, AFR_NUM_CHANGE_LOGS);
        if (!changelog) {
            goto out;
        }
        hton32_1 = htobe32(1);
        for (i = 0; i < priv->child_count; i++) {
            if (local->transaction.failed_subvols[i])
                changelog[i][idx] = hton32_1;
        }
        ret = afr_set_pending_dict(priv, xattr, changelog);
        if (ret < 0) {
            afr_matrix_cleanup(changelog, priv->child_count);
            return NULL;
        }
    }

out:
    return changelog;
}

static void
afr_ta_locked_xattrop_validate(afr_private_t *priv, afr_local_t *local,
                               gf_boolean_t *valid)
{
    if (priv->ta_event_gen > local->ta_event_gen) {
        /* We can't trust the ta's response anymore.*/
        afr_ta_locked_priv_invalidate(priv);
        *valid = _gf_false;
        return;
    }
    return;
}

static int
afr_ta_post_op_do(void *opaque)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    xlator_t *this = NULL;
    dict_t *xattr = NULL;
    unsigned char *pending = NULL;
    int **changelog = NULL;
    int failed_subvol = -1;
    int success_subvol = -1;
    loc_t loc = {
        0,
    };
    int i = 0;
    int ret = 0;
    gf_boolean_t valid = _gf_true;

    local = (afr_local_t *)opaque;
    this = local->transaction.frame->this;
    priv = this->private;

    ret = afr_fill_ta_loc(this, &loc, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate loc for thin-arbiter.");
        goto out;
    }

    xattr = dict_new();
    if (!xattr) {
        ret = -ENOMEM;
        goto out;
    }

    pending = alloca0(priv->child_count);

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.failed_subvols[i]) {
            pending[i] = 1;
            failed_subvol = i;
        } else {
            success_subvol = i;
        }
    }

    changelog = afr_set_changelog_xattr(priv, pending, xattr, local);

    if (!changelog) {
        ret = -ENOMEM;
        goto out;
    }

    ret = afr_ta_post_op_lock(this, &loc);
    if (ret)
        goto out;

    ret = syncop_xattrop(priv->children[THIN_ARBITER_BRICK_INDEX], &loc,
                         GF_XATTROP_ADD_ARRAY, xattr, NULL, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Post-op on thin-arbiter id file %s failed for gfid %s.",
               priv->pending_key[THIN_ARBITER_BRICK_INDEX],
               uuid_utoa(local->inode->gfid));
    }
    LOCK(&priv->lock);
    {
        if (ret == 0) {
            priv->ta_bad_child_index = failed_subvol;
        } else if (ret == -EINVAL) {
            priv->ta_bad_child_index = success_subvol;
            ret = -EIO; /* TA failed the fop. Return EIO to application. */
        }

        afr_ta_locked_xattrop_validate(priv, local, &valid);
    }
    UNLOCK(&priv->lock);
    if (valid == _gf_false) {
        gf_msg(this->name, GF_LOG_ERROR, EIO, AFR_MSG_THIN_ARB,
               "Post-op on thin-arbiter id file %s for gfid %s invalidated due "
               "to event-gen mismatch.",
               priv->pending_key[THIN_ARBITER_BRICK_INDEX],
               uuid_utoa(local->inode->gfid));
        ret = -EIO;
    }

    afr_ta_post_op_unlock(this, &loc);
out:
    if (xattr)
        dict_unref(xattr);

    if (changelog)
        afr_matrix_cleanup(changelog, priv->child_count);

    loc_wipe(&loc);

    return ret;
}

static int
afr_ta_post_op_synctask(xlator_t *this, afr_local_t *local)
{
    call_frame_t *ta_frame = NULL;
    int ret = 0;

    ta_frame = afr_ta_frame_create(this);
    if (!ta_frame) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to create ta_frame");
        goto err;
    }
    ret = synctask_new(this->ctx->env, afr_ta_post_op_do, afr_ta_post_op_done,
                       ta_frame, local);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to launch post-op on thin arbiter for gfid %s",
               uuid_utoa(local->inode->gfid));
        STACK_DESTROY(ta_frame->root);
        goto err;
    }

    return ret;
err:
    afr_changelog_post_op_fail(local->transaction.frame, this, ENOMEM);
    return ret;
}

static void
afr_ta_set_fop_state(afr_private_t *priv, afr_local_t *local,
                     int *on_wire_count)
{
    LOCK(&priv->lock);
    {
        if (priv->release_ta_notify_dom_lock == _gf_true) {
            /* Put the fop in waitq until notify dom lock is released.*/
            local->fop_state = TA_WAIT_FOR_NOTIFY_LOCK_REL;
            list_add_tail(&local->ta_waitq, &priv->ta_waitq);
        } else if (priv->ta_bad_child_index == AFR_CHILD_UNKNOWN) {
            /* Post-op on thin-arbiter to decide success/failure. */
            local->fop_state = TA_GET_INFO_FROM_TA_FILE;
            *on_wire_count = ++priv->ta_on_wire_txn_count;
            if (*on_wire_count > 1) {
                /*Avoid sending multiple on-wire post-ops on TA*/
                list_add_tail(&local->ta_onwireq, &priv->ta_onwireq);
            }
        } else if (local->ta_failed_subvol == priv->ta_bad_child_index) {
            /* Post-op on TA not needed as the fop failed on the in-memory bad
             * brick. Just mark pending xattrs on the good data brick.*/
            local->fop_state = TA_INFO_IN_MEMORY_SUCCESS;
            priv->ta_in_mem_txn_count++;
        } else {
            /* Post-op on TA not needed as the fop succeeded only on the
             * in-memory bad data brick and not the good one. Fail the fop.*/
            local->fop_state = TA_INFO_IN_MEMORY_FAILED;
            priv->ta_in_mem_txn_count++;
        }
    }
    UNLOCK(&priv->lock);
}

static void
afr_ta_fill_failed_subvol(afr_private_t *priv, afr_local_t *local)
{
    int i = 0;

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.failed_subvols[i]) {
            local->ta_failed_subvol = i;
            break;
        }
    }
}

static void
afr_post_op_handle_success(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;

    local = frame->local;
    if (local->is_new_entry == _gf_true) {
        afr_mark_new_entry_changelog(frame, this);
    }
    afr_changelog_post_op_do(frame, this);

    return;
}

static void
afr_post_op_handle_failure(call_frame_t *frame, xlator_t *this, int op_errno)
{
    afr_changelog_post_op_fail(frame, this, op_errno);

    return;
}

static void
afr_ta_decide_post_op_state(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int on_wire_count = 0;

    priv = this->private;
    local = frame->local;

    afr_ta_set_fop_state(priv, local, &on_wire_count);

    switch (local->fop_state) {
        case TA_GET_INFO_FROM_TA_FILE:
            if (on_wire_count == 1)
                afr_ta_post_op_synctask(this, local);
            /*else, fop is queued in ta_onwireq.*/
            break;
        case TA_WAIT_FOR_NOTIFY_LOCK_REL:
            /*Post releasing the notify lock, we will act on this queue*/
            break;
        case TA_INFO_IN_MEMORY_SUCCESS:
            afr_post_op_handle_success(frame, this);
            break;
        case TA_INFO_IN_MEMORY_FAILED:
            afr_post_op_handle_failure(frame, this, EIO);
            break;
        default:
            break;
    }
    return;
}

static void
afr_handle_failure_using_thin_arbiter(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = this->private;
    afr_local_t *local = frame->local;

    afr_ta_fill_failed_subvol(priv, local);
    gf_msg_debug(this->name, 0,
                 "Fop failed on data brick (%s) for gfid=%s. "
                 "ta info needed to decide fop result.",
                 priv->children[local->ta_failed_subvol]->name,
                 uuid_utoa(local->inode->gfid));
    afr_ta_decide_post_op_state(frame, this);
}

static void
afr_changelog_post_op_do(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = this->private;
    afr_local_t *local = NULL;
    dict_t *xattr = NULL;
    int i = 0;
    int ret = 0;
    int idx = 0;
    int nothing_failed = 1;
    gf_boolean_t need_undirty = _gf_false;
    uint32_t hton32_1;

    afr_handle_quorum(frame, this);
    local = frame->local;
    idx = afr_index_for_transaction_type(local->transaction.type);

    xattr = dict_new();
    if (!xattr) {
        afr_changelog_post_op_fail(frame, this, ENOMEM);
        goto out;
    }

    nothing_failed = afr_txn_nothing_failed(frame, this);

    if (afr_changelog_pre_op_uninherit(frame, this))
        need_undirty = _gf_false;
    else
        need_undirty = _gf_true;

    hton32_1 = htobe32(1);
    if (local->op_ret < 0 && !nothing_failed) {
        if (afr_need_dirty_marking(frame, this)) {
            local->dirty[idx] = hton32_1;
            goto set_dirty;
        }

        afr_changelog_post_op_done(frame, this);
        goto out;
    }

    if (nothing_failed && !need_undirty) {
        afr_changelog_post_op_done(frame, this);
        goto out;
    }

    if (local->transaction.in_flight_sb) {
        afr_changelog_post_op_fail(frame, this,
                                   local->transaction.in_flight_sb_errno);
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.failed_subvols[i])
            local->pending[i][idx] = hton32_1;
    }

    ret = afr_set_pending_dict(priv, xattr, local->pending);
    if (ret < 0) {
        afr_changelog_post_op_fail(frame, this, ENOMEM);
        goto out;
    }

    if (need_undirty)
        local->dirty[idx] = htobe32(-1);
    else
        local->dirty[idx] = 0;

set_dirty:
    ret = dict_set_static_bin(xattr, AFR_DIRTY, local->dirty,
                              sizeof(int) * AFR_NUM_CHANGE_LOGS);
    if (ret) {
        afr_changelog_post_op_fail(frame, this, ENOMEM);
        goto out;
    }

    afr_changelog_do(frame, this, xattr, afr_changelog_post_op_done,
                     AFR_TRANSACTION_POST_OP);
out:
    if (xattr)
        dict_unref(xattr);

    return;
}

static int
afr_changelog_post_op_now(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int failed_count = 0;

    priv = this->private;
    local = frame->local;

    if (priv->thin_arbiter_count) {
        failed_count = AFR_COUNT(local->transaction.failed_subvols,
                                 priv->child_count);
        if (failed_count == 1) {
            afr_handle_failure_using_thin_arbiter(frame, this);
            return 0;
        } else {
            /* Txn either succeeded or failed on both data bricks. Let
             * post_op_do handle it as the case might be. */
        }
    }

    afr_changelog_post_op_do(frame, this);
    return 0;
}

static gf_boolean_t
afr_changelog_pre_op_uninherit(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_inode_ctx_t *ctx = NULL;
    int i = 0;
    gf_boolean_t ret = _gf_false;
    int type = 0;

    local = frame->local;
    priv = this->private;
    ctx = local->inode_ctx;

    type = afr_index_for_transaction_type(local->transaction.type);
    if (type != AFR_DATA_TRANSACTION)
        return !local->transaction.dirtied;

    if (local->transaction.no_uninherit)
        return _gf_false;

    /* This function must be idempotent. So check if we
       were called before and return the same answer again.

       It is important to keep this function idempotent for
       the call in afr_changelog_post_op_safe() to not have
       side effects on the call from afr_changelog_post_op_now()
    */
    if (local->transaction.uninherit_done)
        return local->transaction.uninherit_value;

    LOCK(&local->inode->lock);
    {
        for (i = 0; i < priv->child_count; i++) {
            if (local->transaction.pre_op[i] != ctx->pre_op_done[type][i]) {
                ret = !local->transaction.dirtied;
                goto unlock;
            }
        }

        if (ctx->inherited[type]) {
            ret = _gf_true;
            ctx->inherited[type]--;
        } else if (ctx->on_disk[type]) {
            ret = _gf_false;
            ctx->on_disk[type]--;
        } else {
            /* ASSERT */
            ret = _gf_false;
        }

        if (!ctx->inherited[type] && !ctx->on_disk[type]) {
            for (i = 0; i < priv->child_count; i++)
                ctx->pre_op_done[type][i] = 0;
        }
    }
unlock:
    UNLOCK(&local->inode->lock);

    local->transaction.uninherit_done = _gf_true;
    local->transaction.uninherit_value = ret;

    return ret;
}

static gf_boolean_t
afr_changelog_pre_op_inherit(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    gf_boolean_t ret = _gf_false;
    int type = 0;

    local = frame->local;
    priv = this->private;

    if (local->transaction.type != AFR_DATA_TRANSACTION)
        return _gf_false;

    type = afr_index_for_transaction_type(local->transaction.type);

    LOCK(&local->inode->lock);
    {
        if (!local->inode_ctx->on_disk[type]) {
            /* nothing to inherit yet */
            ret = _gf_false;
            goto unlock;
        }

        for (i = 0; i < priv->child_count; i++) {
            if (local->transaction.pre_op[i] !=
                local->inode_ctx->pre_op_done[type][i]) {
                /* either inherit exactly, or don't */
                ret = _gf_false;
                goto unlock;
            }
        }

        local->inode_ctx->inherited[type]++;

        ret = _gf_true;

        local->transaction.inherited = _gf_true;
    }
unlock:
    UNLOCK(&local->inode->lock);

    return ret;
}

static gf_boolean_t
afr_changelog_pre_op_update(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    gf_boolean_t ret = _gf_false;
    int type = 0;

    local = frame->local;
    priv = this->private;

    if (local->transaction.type == AFR_ENTRY_TRANSACTION ||
        local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION)
        return _gf_false;

    if (local->transaction.inherited)
        /* was already inherited in afr_changelog_pre_op */
        return _gf_false;

    if (!local->transaction.dirtied)
        return _gf_false;

    if (!afr_txn_nothing_failed(frame, this))
        return _gf_false;

    type = afr_index_for_transaction_type(local->transaction.type);

    ret = _gf_false;

    LOCK(&local->inode->lock);
    {
        if (!local->inode_ctx->on_disk[type]) {
            for (i = 0; i < priv->child_count; i++)
                local->inode_ctx->pre_op_done[type][i] =
                    (!local->transaction.failed_subvols[i]);
        } else {
            for (i = 0; i < priv->child_count; i++)
                if (local->inode_ctx->pre_op_done[type][i] !=
                    (!local->transaction.failed_subvols[i])) {
                    local->transaction.no_uninherit = 1;
                    goto unlock;
                }
        }
        local->inode_ctx->on_disk[type]++;

        ret = _gf_true;
    }
unlock:
    UNLOCK(&local->inode->lock);

    return ret;
}

static int
afr_changelog_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, dict_t *xattr, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = -1;
    int child_index = -1;

    local = frame->local;
    child_index = (long)cookie;

    if (op_ret == -1) {
        local->op_errno = op_errno;
        afr_transaction_fop_failed(frame, this, child_index);
    }

    if (xattr)
        local->transaction.changelog_xdata[child_index] = dict_ref(xattr);

    call_count = afr_frame_return(frame);

    if (call_count == 0) {
        local->transaction.changelog_resume(frame, this);
    }

    return 0;
}

static void
afr_changelog_populate_xdata(call_frame_t *frame, afr_xattrop_type_t op,
                             dict_t **xdata, dict_t **newloc_xdata)
{
    int i = 0;
    int ret = 0;
    char *key = NULL;
    int keylen = 0;
    const char *name = NULL;
    dict_t *xdata1 = NULL;
    dict_t *xdata2 = NULL;
    xlator_t *this = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    gf_boolean_t need_entry_key_set = _gf_true;

    local = frame->local;
    this = THIS;
    priv = this->private;

    if (local->transaction.type == AFR_DATA_TRANSACTION ||
        local->transaction.type == AFR_METADATA_TRANSACTION)
        goto out;

    if (!priv->esh_granular)
        goto out;

    xdata1 = dict_new();
    if (!xdata1)
        goto out;

    name = local->loc.name;
    if (local->op == GF_FOP_LINK)
        name = local->newloc.name;

    switch (op) {
        case AFR_TRANSACTION_PRE_OP:
            key = GF_XATTROP_ENTRY_IN_KEY;
            break;
        case AFR_TRANSACTION_POST_OP:
            if (afr_txn_nothing_failed(frame, this)) {
                key = GF_XATTROP_ENTRY_OUT_KEY;
                for (i = 0; i < priv->child_count; i++) {
                    if (!local->transaction.failed_subvols[i])
                        continue;
                    need_entry_key_set = _gf_false;
                    break;
                }
                /* If the transaction itself did not fail and there
                 * are no failed subvolumes, check whether the fop
                 * failed due to a symmetric error. If it did, do
                 * not set the ENTRY_OUT xattr which would end up
                 * deleting a name index which was created possibly by
                 * an earlier entry txn that may have failed on some
                 * of the sub-volumes.
                 */
                if (local->op_ret)
                    need_entry_key_set = _gf_false;
            } else {
                key = GF_XATTROP_ENTRY_IN_KEY;
            }
            break;
    }

    if (need_entry_key_set) {
        keylen = strlen(key);
        ret = dict_set_strn(xdata1, key, keylen, (char *)name);
        if (ret)
            gf_msg(THIS->name, GF_LOG_ERROR, 0, AFR_MSG_DICT_SET_FAILED,
                   "%s/%s: Could not set %s key during xattrop",
                   uuid_utoa(local->loc.pargfid), local->loc.name, key);
        if (local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) {
            xdata2 = dict_new();
            if (!xdata2)
                goto out;

            ret = dict_set_strn(xdata2, key, keylen,
                                (char *)local->newloc.name);
            if (ret)
                gf_msg(THIS->name, GF_LOG_ERROR, 0, AFR_MSG_DICT_SET_FAILED,
                       "%s/%s: Could not set %s key during "
                       "xattrop",
                       uuid_utoa(local->newloc.pargfid), local->newloc.name,
                       key);
        }
    }

    *xdata = xdata1;
    *newloc_xdata = xdata2;
    xdata1 = xdata2 = NULL;
out:
    if (xdata1)
        dict_unref(xdata1);
    return;
}

static int
afr_changelog_prepare(xlator_t *this, call_frame_t *frame, int *call_count,
                      afr_changelog_resume_t changelog_resume,
                      afr_xattrop_type_t op, dict_t **xdata,
                      dict_t **newloc_xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    local = frame->local;
    priv = this->private;

    *call_count = afr_changelog_call_count(
        local->transaction.type, local->transaction.pre_op,
        local->transaction.failed_subvols, priv->child_count);

    if (*call_count == 0) {
        changelog_resume(frame, this);
        return -1;
    }

    afr_changelog_populate_xdata(frame, op, xdata, newloc_xdata);
    local->call_count = *call_count;

    local->transaction.changelog_resume = changelog_resume;
    return 0;
}

static int
afr_changelog_do(call_frame_t *frame, xlator_t *this, dict_t *xattr,
                 afr_changelog_resume_t changelog_resume, afr_xattrop_type_t op)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    dict_t *xdata = NULL;
    dict_t *newloc_xdata = NULL;
    int i = 0;
    int call_count = 0;
    int ret = 0;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (local->transaction.changelog_xdata[i]) {
            dict_unref(local->transaction.changelog_xdata[i]);
            local->transaction.changelog_xdata[i] = NULL;
        }
    }

    ret = afr_changelog_prepare(this, frame, &call_count, changelog_resume, op,
                                &xdata, &newloc_xdata);

    if (ret)
        return 0;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->transaction.pre_op[i] ||
            local->transaction.failed_subvols[i])
            continue;

        switch (local->transaction.type) {
            case AFR_DATA_TRANSACTION:
            case AFR_METADATA_TRANSACTION:
                if (!local->fd) {
                    STACK_WIND_COOKIE(
                        frame, afr_changelog_cbk, (void *)(long)i,
                        priv->children[i], priv->children[i]->fops->xattrop,
                        &local->loc, GF_XATTROP_ADD_ARRAY, xattr, xdata);
                } else {
                    STACK_WIND_COOKIE(
                        frame, afr_changelog_cbk, (void *)(long)i,
                        priv->children[i], priv->children[i]->fops->fxattrop,
                        local->fd, GF_XATTROP_ADD_ARRAY, xattr, xdata);
                }
                break;
            case AFR_ENTRY_RENAME_TRANSACTION:

                STACK_WIND_COOKIE(frame, afr_changelog_cbk, (void *)(long)i,
                                  priv->children[i],
                                  priv->children[i]->fops->xattrop,
                                  &local->transaction.new_parent_loc,
                                  GF_XATTROP_ADD_ARRAY, xattr, newloc_xdata);
                call_count--;

                /* fall through */

            case AFR_ENTRY_TRANSACTION:
                if (local->fd)
                    STACK_WIND_COOKIE(
                        frame, afr_changelog_cbk, (void *)(long)i,
                        priv->children[i], priv->children[i]->fops->fxattrop,
                        local->fd, GF_XATTROP_ADD_ARRAY, xattr, xdata);
                else
                    STACK_WIND_COOKIE(frame, afr_changelog_cbk, (void *)(long)i,
                                      priv->children[i],
                                      priv->children[i]->fops->xattrop,
                                      &local->transaction.parent_loc,
                                      GF_XATTROP_ADD_ARRAY, xattr, xdata);
                break;
        }

        if (!--call_count)
            break;
    }

    if (xdata)
        dict_unref(xdata);
    if (newloc_xdata)
        dict_unref(newloc_xdata);
    return 0;
}

static void
afr_init_optimistic_changelog_for_txn(xlator_t *this, afr_local_t *local)
{
    int locked_count = 0;
    afr_private_t *priv = NULL;

    priv = this->private;

    locked_count = AFR_COUNT(local->transaction.pre_op, priv->child_count);
    if (priv->optimistic_change_log && locked_count == priv->child_count)
        local->optimistic_change_log = 1;

    return;
}

int
afr_changelog_pre_op(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = this->private;
    int i = 0;
    int ret = 0;
    int call_count = 0;
    int op_errno = 0;
    afr_local_t *local = NULL;
    afr_internal_lock_t *int_lock = NULL;
    unsigned char *locked_nodes = NULL;
    int idx = -1;
    gf_boolean_t pre_nop = _gf_true;
    dict_t *xdata_req = NULL;

    local = frame->local;
    int_lock = &local->internal_lock;
    idx = afr_index_for_transaction_type(local->transaction.type);

    locked_nodes = afr_locked_nodes_get(local->transaction.type, int_lock);

    for (i = 0; i < priv->child_count; i++) {
        if (locked_nodes[i]) {
            local->transaction.pre_op[i] = 1;
            call_count++;
        } else {
            local->transaction.failed_subvols[i] = 1;
        }
    }

    afr_init_optimistic_changelog_for_txn(this, local);

    if (afr_changelog_pre_op_inherit(frame, this))
        goto next;

    /* This condition should not be met with present code, as
     * transaction.done will be called if locks are not acquired on even a
     * single node.
     */
    if (call_count == 0) {
        op_errno = ENOTCONN;
        goto err;
    }

    /* Check if the fop can be performed on at least
     * quorum number of nodes.
     */
    if (priv->quorum_count && !afr_has_fop_quorum(frame)) {
        op_errno = int_lock->lock_op_errno;
        if (op_errno == 0)
            op_errno = afr_quorum_errno(priv);
        goto err;
    }

    xdata_req = dict_new();
    if (!xdata_req) {
        op_errno = ENOMEM;
        goto err;
    }

    if (call_count < priv->child_count)
        pre_nop = _gf_false;

    /* Set an all-zero pending changelog so that in the cbk, we can get the
     * current on-disk values. In a replica 3 volume with arbiter enabled,
     * these values are needed to arrive at a go/ no-go of the fop phase to
     * avoid ending up in split-brain.*/

    ret = afr_set_pending_dict(priv, xdata_req, local->pending);
    if (ret < 0) {
        op_errno = ENOMEM;
        goto err;
    }

    if (afr_needs_changelog_update(local)) {
        local->dirty[idx] = htobe32(1);

        ret = dict_set_static_bin(xdata_req, AFR_DIRTY, local->dirty,
                                  sizeof(int) * AFR_NUM_CHANGE_LOGS);
        if (ret) {
            op_errno = ENOMEM;
            goto err;
        }

        pre_nop = _gf_false;
        local->transaction.dirtied = 1;
    }

    if (pre_nop)
        goto next;

    if (!local->pre_op_compat) {
        dict_copy(xdata_req, local->xdata_req);
        goto next;
    }

    afr_changelog_do(frame, this, xdata_req, afr_transaction_perform_fop,
                     AFR_TRANSACTION_PRE_OP);

    if (xdata_req)
        dict_unref(xdata_req);

    return 0;
next:
    afr_transaction_perform_fop(frame, this);

    if (xdata_req)
        dict_unref(xdata_req);

    return 0;
err:
    local->internal_lock.lock_cbk = afr_transaction_done;
    local->op_ret = -1;
    local->op_errno = op_errno;

    afr_handle_lock_acquire_failure(local);

    if (xdata_req)
        dict_unref(xdata_req);

    return 0;
}

static int
afr_post_nonblocking_lock_cbk(call_frame_t *frame, xlator_t *this)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;

    local = frame->local;
    int_lock = &local->internal_lock;

    /* Initiate blocking locks if non-blocking has failed */
    if (int_lock->lock_op_ret < 0) {
        gf_msg_debug(this->name, 0,
                     "Non blocking locks failed. Proceeding to blocking");
        int_lock->lock_cbk = afr_internal_lock_finish;
        afr_blocking_lock(frame, this);
    } else {
        gf_msg_debug(this->name, 0,
                     "Non blocking locks done. Proceeding to FOP");

        afr_internal_lock_finish(frame, this);
    }

    return 0;
}

static int
afr_set_transaction_flock(xlator_t *this, afr_local_t *local,
                          afr_lockee_t *lockee)
{
    afr_private_t *priv = NULL;
    struct gf_flock *flock = NULL;

    priv = this->private;
    flock = &lockee->flock;

    if ((priv->arbiter_count || local->transaction.eager_lock_on ||
         priv->full_lock) &&
        local->transaction.type == AFR_DATA_TRANSACTION) {
        /*Lock entire file to avoid network split brains.*/
        flock->l_len = 0;
        flock->l_start = 0;
    } else {
        flock->l_len = local->transaction.len;
        flock->l_start = local->transaction.start;
    }
    flock->l_type = F_WRLCK;

    return 0;
}

int
afr_lock(call_frame_t *frame, xlator_t *this)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    int i = 0;

    local = frame->local;
    int_lock = &local->internal_lock;

    int_lock->lock_cbk = afr_post_nonblocking_lock_cbk;
    int_lock->domain = this->name;

    switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
            for (i = 0; i < int_lock->lockee_count; i++) {
                afr_set_transaction_flock(this, local, &int_lock->lockee[i]);
            }

            break;

        case AFR_ENTRY_TRANSACTION:
            if (local->transaction.parent_loc.path)
                int_lock->lk_loc = &local->transaction.parent_loc;
            else
                GF_ASSERT(local->fd);
            break;
        case AFR_ENTRY_RENAME_TRANSACTION:
            break;
    }
    afr_lock_nonblocking(frame, this);

    return 0;
}

static gf_boolean_t
afr_locals_overlap(afr_local_t *local1, afr_local_t *local2)
{
    uint64_t start1 = local1->transaction.start;
    uint64_t start2 = local2->transaction.start;
    uint64_t end1 = 0;
    uint64_t end2 = 0;

    if (local1->transaction.len)
        end1 = start1 + local1->transaction.len - 1;
    else
        end1 = ULLONG_MAX;

    if (local2->transaction.len)
        end2 = start2 + local2->transaction.len - 1;
    else
        end2 = ULLONG_MAX;

    return ((end1 >= start2) && (end2 >= start1));
}

gf_boolean_t
afr_has_lock_conflict(afr_local_t *local, gf_boolean_t waitlist_check)
{
    afr_local_t *each = NULL;
    afr_lock_t *lock = NULL;

    lock = &local->inode_ctx->lock[local->transaction.type];
    /*
     * Once full file lock is acquired in eager-lock phase, overlapping
     * writes do not compete for inode-locks, instead are transferred to the
     * next writes. Because of this overlapping writes are not ordered.
     * This can cause inconsistencies in replication.
     * Example:
     * Two overlapping writes w1, w2 are sent in parallel on same fd
     * in two threads t1, t2.
     * Both threads can execute afr_writev_wind in the following manner.
     * t1 winds w1 on brick-0
     * t2 winds w2 on brick-0
     * t2 winds w2 on brick-1
     * t1 winds w1 on brick-1
     *
     * This check makes sure the locks are not transferred for
     * overlapping writes.
     */
    list_for_each_entry(each, &lock->owners, transaction.owner_list)
    {
        if (afr_locals_overlap(each, local)) {
            return _gf_true;
        }
    }

    if (!waitlist_check)
        return _gf_false;
    list_for_each_entry(each, &lock->waiting, transaction.wait_list)
    {
        if (afr_locals_overlap(each, local)) {
            return _gf_true;
        }
    }
    return _gf_false;
}

/* }}} */
static void
afr_copy_inodelk_vars(afr_internal_lock_t *dst, afr_internal_lock_t *src,
                      xlator_t *this, int lockee_num)
{
    afr_private_t *priv = this->private;
    afr_lockee_t *sl = &src->lockee[lockee_num];
    afr_lockee_t *dl = &dst->lockee[lockee_num];

    dst->domain = src->domain;
    dl->flock.l_len = sl->flock.l_len;
    dl->flock.l_start = sl->flock.l_start;
    dl->flock.l_type = sl->flock.l_type;
    dl->locked_count = sl->locked_count;
    memcpy(dl->locked_nodes, sl->locked_nodes,
           priv->child_count * sizeof(*dl->locked_nodes));
}

static void
__afr_transaction_wake_shared(afr_local_t *local, struct list_head *shared)
{
    gf_boolean_t conflict = _gf_false;
    afr_local_t *each = NULL;
    afr_lock_t *lock = &local->inode_ctx->lock[local->transaction.type];

    while (!conflict) {
        if (list_empty(&lock->waiting))
            return;
        each = list_entry(lock->waiting.next, afr_local_t,
                          transaction.wait_list);
        if (afr_has_lock_conflict(each, _gf_false)) {
            conflict = _gf_true;
        }
        if (conflict && !list_empty(&lock->owners))
            return;
        afr_copy_inodelk_vars(&each->internal_lock, &local->internal_lock,
                              each->transaction.frame->this, 0);
        list_move_tail(&each->transaction.wait_list, shared);
        list_add_tail(&each->transaction.owner_list, &lock->owners);
    }
}

static void
afr_lock_resume_shared(struct list_head *list)
{
    afr_local_t *each = NULL;

    while (!list_empty(list)) {
        each = list_entry(list->next, afr_local_t, transaction.wait_list);
        list_del_init(&each->transaction.wait_list);
        afr_changelog_pre_op(each->transaction.frame,
                             each->transaction.frame->this);
    }
}

static int
afr_internal_lock_finish(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = frame->local;
    afr_lock_t *lock = NULL;

    local->internal_lock.lock_cbk = NULL;
    if (!local->transaction.eager_lock_on) {
        if (local->internal_lock.lock_op_ret < 0) {
            afr_transaction_done(frame, this);
            return 0;
        }
        afr_changelog_pre_op(frame, this);
    } else {
        lock = &local->inode_ctx->lock[local->transaction.type];
        if (local->internal_lock.lock_op_ret < 0) {
            afr_handle_lock_acquire_failure(local);
        } else {
            lock->event_generation = local->event_generation;
            afr_changelog_pre_op(frame, this);
        }
    }

    return 0;
}

static gf_boolean_t
afr_are_conflicting_ops_waiting(afr_local_t *local, xlator_t *this)
{
    afr_lock_t *lock = NULL;
    lock = &local->inode_ctx->lock[local->transaction.type];

    /* Lets say mount1 has eager-lock(full-lock) and after the eager-lock
     * is taken mount2 opened the same file, it won't be able to
     * perform any {meta,}data operations until mount1 releases eager-lock.
     * To avoid such scenario do not enable eager-lock for this transaction
     * if open-fd-count is > 1 for metadata transactions and if num-inodelks > 1
     * for data transactions
     */

    if (local->transaction.type == AFR_METADATA_TRANSACTION) {
        if (local->inode_ctx->open_fd_count > 1) {
            return _gf_true;
        }
    } else if (local->transaction.type == AFR_DATA_TRANSACTION) {
        if (lock->num_inodelks > 1) {
            return _gf_true;
        }
    }

    return _gf_false;
}

static gf_boolean_t
afr_is_delayed_changelog_post_op_needed(call_frame_t *frame, xlator_t *this,
                                        int delay)
{
    afr_local_t *local = NULL;
    afr_lock_t *lock = NULL;
    gf_boolean_t res = _gf_false;

    local = frame->local;
    lock = &local->inode_ctx->lock[local->transaction.type];

    if (!afr_txn_nothing_failed(frame, this)) {
        lock->release = _gf_true;
        goto out;
    }

    if (afr_are_conflicting_ops_waiting(local, this)) {
        lock->release = _gf_true;
        goto out;
    }

    if (!list_empty(&lock->owners))
        goto out;
    else
        GF_ASSERT(list_empty(&lock->waiting));

    if (lock->release) {
        goto out;
    }

    if (!delay) {
        goto out;
    }

    if (local->transaction.disable_delayed_post_op) {
        goto out;
    }

    if ((local->op != GF_FOP_WRITE) && (local->op != GF_FOP_FXATTROP) &&
        (local->op != GF_FOP_FSYNC)) {
        /*Only allow writes/fsyncs but shard does [f]xattrops on writes, so
         * they are fine too*/
        goto out;
    }

    res = _gf_true;
out:
    return res;
}

void
afr_delayed_changelog_wake_up_cbk(void *data)
{
    afr_lock_t *lock = NULL;
    afr_local_t *local = data;
    afr_local_t *timer_local = NULL;
    struct list_head shared;

    INIT_LIST_HEAD(&shared);
    lock = &local->inode_ctx->lock[local->transaction.type];
    LOCK(&local->inode->lock);
    {
        timer_local = list_entry(lock->post_op.next, afr_local_t,
                                 transaction.owner_list);
        if (list_empty(&lock->owners) && (local == timer_local)) {
            GF_ASSERT(list_empty(&lock->waiting));
            /*Last owner*/
            lock->release = _gf_true;
            lock->delay_timer = NULL;
        }
    }
    UNLOCK(&local->inode->lock);
    afr_changelog_post_op_now(local->transaction.frame,
                              local->transaction.frame->this);
}

/* SET operation */
int
afr_fd_report_unstable_write(xlator_t *this, afr_local_t *local)
{
    LOCK(&local->inode->lock);
    {
        local->inode_ctx->witnessed_unstable_write = _gf_true;
    }
    UNLOCK(&local->inode->lock);

    return 0;
}

/* TEST and CLEAR operation */
gf_boolean_t
afr_fd_has_witnessed_unstable_write(xlator_t *this, inode_t *inode)
{
    afr_inode_ctx_t *ctx = NULL;
    gf_boolean_t witness = _gf_false;

    LOCK(&inode->lock);
    {
        ctx = __afr_inode_ctx_get(this, inode);
        if (ctx == NULL)
            goto unlock;

        if (ctx->witnessed_unstable_write) {
            witness = _gf_true;
            ctx->witnessed_unstable_write = _gf_false;
        }
    }
unlock:
    UNLOCK(&inode->lock);

    return witness;
}

static int
afr_changelog_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, struct iatt *pre,
                        struct iatt *post, dict_t *xdata)
{
    afr_private_t *priv = NULL;
    int child_index = (long)cookie;
    int call_count = -1;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    if (op_ret != 0) {
        /* Failure of fsync() is as good as failure of previous
           write(). So treat it like one.
        */
        gf_msg(this->name, GF_LOG_WARNING, op_errno, AFR_MSG_FSYNC_FAILED,
               "fsync(%s) failed on subvolume %s. Transaction was %s",
               uuid_utoa(local->fd->inode->gfid),
               priv->children[child_index]->name, gf_fop_list[local->op]);

        afr_transaction_fop_failed(frame, this, child_index);
    }

    call_count = afr_frame_return(frame);

    if (call_count == 0)
        afr_changelog_post_op_now(frame, this);

    return 0;
}

static int
afr_changelog_fsync(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    int i = 0;
    int call_count = 0;
    afr_private_t *priv = NULL;
    dict_t *xdata = NULL;
    GF_UNUSED int ret = -1;

    local = frame->local;
    priv = this->private;

    call_count = AFR_COUNT(local->transaction.pre_op, priv->child_count);

    if (!call_count) {
        /* will go straight to unlock */
        afr_changelog_post_op_now(frame, this);
        return 0;
    }

    local->call_count = call_count;

    xdata = dict_new();
    if (xdata) {
        ret = dict_set_int32_sizen(xdata, "batch-fsync", 1);
        ret = dict_set_str(xdata, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!local->transaction.pre_op[i])
            continue;

        STACK_WIND_COOKIE(frame, afr_changelog_fsync_cbk, (void *)(long)i,
                          priv->children[i], priv->children[i]->fops->fsync,
                          local->fd, 1, xdata);
        if (!--call_count)
            break;
    }

    if (xdata)
        dict_unref(xdata);

    return 0;
}

static int
afr_changelog_post_op_safe(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    if (!local->fd || local->transaction.type != AFR_DATA_TRANSACTION) {
        afr_changelog_post_op_now(frame, this);
        return 0;
    }

    if (afr_changelog_pre_op_uninherit(frame, this) &&
        afr_txn_nothing_failed(frame, this)) {
        /* just detected that this post-op is about to
           be optimized away as a new write() has
           already piggybacked on this frame's changelog.
           */
        afr_changelog_post_op_now(frame, this);
        return 0;
    }

    /* Calling afr_changelog_post_op_now() now will result in
       issuing ->[f]xattrop().

       Performing a hard POST-OP (->[f]xattrop() FOP) is a more
       responsible operation that what it might appear on the surface.

       The changelog of a file (in the xattr of the file on the server)
       stores information (pending count) about the state of the file
       on the OTHER server. This changelog is blindly trusted, and must
       therefore be updated in such a way it remains trustworthy. This
       implies that decrementing the pending count (essentially "clearing
       the dirty flag") must be done STRICTLY after we are sure that the
       operation on the other server has reached stable storage.

       While the backend filesystem on that server will eventually flush
       it to stable storage, we (being in userspace) have no mechanism
       to get notified when the write became "stable".

       This means we need take matter into our own hands and issue an
       fsync() EVEN IF THE APPLICATION WAS PERFORMING UNSTABLE WRITES,
       and get an acknowledgement for it. And we need to wait for the
       fsync() acknowledgement before initiating the hard POST-OP.

       However if the FD itself was opened in O_SYNC or O_DSYNC then
       we are already guaranteed that the writes were made stable as
       part of the FOP itself. The same holds true for NFS stable
       writes which happen on an anonymous FD with O_DSYNC or O_SYNC
       flag set in the writev() @flags param. For all other write types,
       mark a flag in the fdctx whenever an unstable write is witnessed.
       */

    if (!afr_fd_has_witnessed_unstable_write(this, local->inode)) {
        afr_changelog_post_op_now(frame, this);
        return 0;
    }

    /* Check whether users want durability and perform fsync/post-op
     * accordingly.
     */
    if (priv->ensure_durability) {
        /* Time to fsync() */
        afr_changelog_fsync(frame, this);
    } else {
        afr_changelog_post_op_now(frame, this);
    }

    return 0;
}

void
afr_changelog_post_op(call_frame_t *frame, xlator_t *this)
{
    struct timespec delta = {
        0,
    };
    afr_private_t *priv = NULL;
    afr_local_t *local = frame->local;
    afr_lock_t *lock = NULL;
    gf_boolean_t post_op = _gf_true;
    struct list_head shared;

    priv = this->private;
    delta.tv_sec = priv->post_op_delay_secs;
    delta.tv_nsec = 0;

    INIT_LIST_HEAD(&shared);
    if (!local->transaction.eager_lock_on)
        goto out;

    lock = &local->inode_ctx->lock[local->transaction.type];
    LOCK(&local->inode->lock);
    {
        list_del_init(&local->transaction.owner_list);
        list_add(&local->transaction.owner_list, &lock->post_op);
        __afr_transaction_wake_shared(local, &shared);

        if (!afr_is_delayed_changelog_post_op_needed(frame, this,
                                                     delta.tv_sec)) {
            if (list_empty(&lock->owners))
                lock->release = _gf_true;
            goto unlock;
        }

        GF_ASSERT(lock->delay_timer == NULL);
        lock->delay_timer = gf_timer_call_after(
            this->ctx, delta, afr_delayed_changelog_wake_up_cbk, local);
        if (!lock->delay_timer) {
            lock->release = _gf_true;
        } else {
            post_op = _gf_false;
        }
    }
unlock:
    UNLOCK(&local->inode->lock);

    if (!list_empty(&shared)) {
        afr_lock_resume_shared(&shared);
    }

out:
    if (post_op) {
        if (!local->transaction.eager_lock_on || lock->release) {
            afr_changelog_post_op_safe(frame, this);
        } else {
            afr_changelog_post_op_now(frame, this);
        }
    }
}

int
afr_transaction_resume(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;

    local = frame->local;

    afr_restore_lk_owner(frame);

    afr_handle_symmetric_errors(frame, this);

    if (!local->pre_op_compat)
        /* new mode, pre-op was done along
           with OP */
        afr_changelog_pre_op_update(frame, this);

    afr_changelog_post_op(frame, this);

    return 0;
}

/**
 * afr_transaction_fop_failed - inform that an fop failed
 */

void
afr_transaction_fop_failed(call_frame_t *frame, xlator_t *this, int child_index)
{
    afr_local_t *local = NULL;

    local = frame->local;

    local->transaction.failed_subvols[child_index] = 1;
}

static gf_boolean_t
__need_previous_lock_unlocked(afr_local_t *local)
{
    afr_lock_t *lock = NULL;

    lock = &local->inode_ctx->lock[local->transaction.type];
    if (!lock->acquired)
        return _gf_false;
    if (lock->acquired && lock->event_generation != local->event_generation)
        return _gf_true;
    return _gf_false;
}

static void
__afr_eager_lock_handle(afr_local_t *local, gf_boolean_t *take_lock,
                        gf_boolean_t *do_pre_op, afr_local_t **timer_local)
{
    afr_lock_t *lock = NULL;
    afr_local_t *owner_local = NULL;
    xlator_t *this = local->transaction.frame->this;

    local->transaction.eager_lock_on = _gf_true;
    afr_set_lk_owner(local->transaction.frame, this, local->inode);

    lock = &local->inode_ctx->lock[local->transaction.type];
    if (__need_previous_lock_unlocked(local)) {
        if (!list_empty(&lock->owners)) {
            lock->release = _gf_true;
        } else if (lock->delay_timer) {
            lock->release = _gf_true;
            if (gf_timer_call_cancel(this->ctx, lock->delay_timer)) {
                /* It will be put in frozen list
                 * in the code flow below*/
            } else {
                *timer_local = list_entry(lock->post_op.next, afr_local_t,
                                          transaction.owner_list);
                lock->delay_timer = NULL;
            }
        }
    }

    if (lock->release) {
        list_add_tail(&local->transaction.wait_list, &lock->frozen);
        *take_lock = _gf_false;
        goto out;
    }

    if (lock->delay_timer) {
        *take_lock = _gf_false;
        if (gf_timer_call_cancel(this->ctx, lock->delay_timer)) {
            list_add_tail(&local->transaction.wait_list, &lock->frozen);
        } else {
            *timer_local = list_entry(lock->post_op.next, afr_local_t,
                                      transaction.owner_list);
            afr_copy_inodelk_vars(&local->internal_lock,
                                  &(*timer_local)->internal_lock, this, 0);
            lock->delay_timer = NULL;
            *do_pre_op = _gf_true;
            list_add_tail(&local->transaction.owner_list, &lock->owners);
        }
        goto out;
    }

    if (!list_empty(&lock->owners)) {
        if (!lock->acquired || afr_has_lock_conflict(local, _gf_true)) {
            list_add_tail(&local->transaction.wait_list, &lock->waiting);
            *take_lock = _gf_false;
            goto out;
        }
        owner_local = list_entry(lock->owners.next, afr_local_t,
                                 transaction.owner_list);
        afr_copy_inodelk_vars(&local->internal_lock,
                              &owner_local->internal_lock, this, 0);
        *take_lock = _gf_false;
        *do_pre_op = _gf_true;
    }

    if (lock->acquired)
        GF_ASSERT(!(*take_lock));
    list_add_tail(&local->transaction.owner_list, &lock->owners);
out:
    return;
}

static void
afr_transaction_start(afr_local_t *local, xlator_t *this)
{
    afr_private_t *priv = NULL;
    gf_boolean_t take_lock = _gf_true;
    gf_boolean_t do_pre_op = _gf_false;
    afr_local_t *timer_local = NULL;

    priv = this->private;

    if (local->transaction.type != AFR_DATA_TRANSACTION &&
        local->transaction.type != AFR_METADATA_TRANSACTION)
        goto lock_phase;

    if (!priv->eager_lock)
        goto lock_phase;

    LOCK(&local->inode->lock);
    {
        __afr_eager_lock_handle(local, &take_lock, &do_pre_op, &timer_local);
    }
    UNLOCK(&local->inode->lock);
lock_phase:
    if (!local->transaction.eager_lock_on) {
        afr_set_lk_owner(local->transaction.frame, this,
                         local->transaction.frame->root);
    }

    if (take_lock) {
        afr_lock(local->transaction.frame, this);
    } else if (do_pre_op) {
        afr_changelog_pre_op(local->transaction.frame, this);
    }
    /*Always call delayed_changelog_wake_up_cbk after calling pre-op above
     * so that any inheriting can happen*/
    if (timer_local)
        afr_delayed_changelog_wake_up_cbk(timer_local);
}

static int
afr_write_txn_refresh_done(call_frame_t *frame, xlator_t *this, int err)
{
    afr_local_t *local = frame->local;

    if (err) {
        AFR_SET_ERROR_AND_CHECK_SPLIT_BRAIN(-1, err);
        goto fail;
    }

    afr_transaction_start(local, this);
    return 0;
fail:
    local->transaction.unwind(frame, this);
    AFR_STACK_DESTROY(frame);
    return 0;
}

static int
afr_transaction_lockee_init(call_frame_t *frame)
{
    afr_local_t *local = frame->local;
    afr_internal_lock_t *int_lock = &local->internal_lock;
    afr_private_t *priv = frame->this->private;
    int ret = 0;

    switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
            ret = afr_add_inode_lockee(local, priv->child_count);
            break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
            ret = afr_add_entry_lockee(local, &local->transaction.parent_loc,
                                       local->transaction.basename,
                                       priv->child_count);
            if (ret) {
                goto out;
            }
            if (local->op == GF_FOP_RENAME) {
                ret = afr_add_entry_lockee(
                    local, &local->transaction.new_parent_loc,
                    local->transaction.new_basename, priv->child_count);
                if (ret) {
                    goto out;
                }

                if (local->newloc.inode &&
                    IA_ISDIR(local->newloc.inode->ia_type)) {
                    ret = afr_add_entry_lockee(local, &local->newloc, NULL,
                                               priv->child_count);
                    if (ret) {
                        goto out;
                    }
                }
            } else if (local->op == GF_FOP_RMDIR) {
                ret = afr_add_entry_lockee(local, &local->loc, NULL,
                                           priv->child_count);
                if (ret) {
                    goto out;
                }
            }

            if (int_lock->lockee_count > 1) {
                qsort(int_lock->lockee, int_lock->lockee_count,
                      sizeof(*int_lock->lockee), afr_entry_lockee_cmp);
            }
            break;
    }
out:
    return ret;
}

int
afr_transaction(call_frame_t *frame, xlator_t *this, afr_transaction_type type)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int ret = -1;
    int event_generation = 0;

    local = frame->local;
    priv = this->private;
    local->transaction.frame = frame;

    local->transaction.type = type;

    if (priv->quorum_count && !afr_has_quorum(local->child_up, this, NULL)) {
        ret = -afr_quorum_errno(priv);
        goto out;
    }

    if (!afr_is_consistent_io_possible(local, priv, &ret)) {
        ret = -ret; /*op_errno to ret conversion*/
        goto out;
    }

    if (priv->thin_arbiter_count && !afr_ta_has_quorum(priv, local)) {
        ret = -afr_quorum_errno(priv);
        goto out;
    }

    ret = afr_transaction_local_init(local, this);
    if (ret < 0)
        goto out;

    ret = afr_transaction_lockee_init(frame);
    if (ret)
        goto out;

    if (type != AFR_METADATA_TRANSACTION) {
        goto txn_start;
    }

    ret = afr_inode_get_readable(frame, local->inode, this, local->readable,
                                 &event_generation, type);
    if (ret < 0 ||
        afr_is_inode_refresh_reqd(local->inode, this, priv->event_generation,
                                  event_generation)) {
        afr_inode_refresh(frame, this, local->inode, local->loc.gfid,
                          afr_write_txn_refresh_done);
        ret = 0;
        goto out;
    }

txn_start:
    ret = 0;
    afr_transaction_start(local, this);
out:
    return ret;
}
