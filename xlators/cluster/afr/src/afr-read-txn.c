/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "afr.h"
#include "afr-transaction.h"
#include "afr-messages.h"

void
afr_pending_read_increment(afr_private_t *priv, int child_index)
{
    if (child_index < 0 || child_index > priv->child_count)
        return;

    GF_ATOMIC_INC(priv->pending_reads[child_index]);
}

void
afr_pending_read_decrement(afr_private_t *priv, int child_index)
{
    if (child_index < 0 || child_index > priv->child_count)
        return;

    GF_ATOMIC_DEC(priv->pending_reads[child_index]);
}

void
afr_read_txn_wind(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    afr_pending_read_decrement(priv, local->read_subvol);
    local->read_subvol = subvol;
    afr_pending_read_increment(priv, subvol);
    local->readfn(frame, this, subvol);
}

int
afr_read_txn_next_subvol(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int subvol = -1;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->readable[i]) {
            /* don't even bother trying here.
               just mark as attempted and move on. */
            local->read_attempted[i] = 1;
            continue;
        }

        if (!local->read_attempted[i]) {
            subvol = i;
            break;
        }
    }

    /* If no more subvols were available for reading, we leave
       @subvol as -1, which is an indication we have run out of
       readable subvols. */
    if (subvol != -1)
        local->read_attempted[subvol] = 1;
    afr_read_txn_wind(frame, this, subvol);

    return 0;
}

static int
afr_ta_read_txn_done(int ret, call_frame_t *ta_frame, void *opaque)
{
    STACK_DESTROY(ta_frame->root);
    return 0;
}

static int
afr_ta_read_txn(void *opaque)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    int read_subvol = -1;
    int query_child = AFR_CHILD_UNKNOWN;
    int possible_bad_child = AFR_CHILD_UNKNOWN;
    int ret = 0;
    int op_errno = ENOMEM;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    struct gf_flock flock = {
        0,
    };
    dict_t *xdata_req = NULL;
    dict_t *xdata_rsp = NULL;
    int **pending = NULL;
    loc_t loc = {
        0,
    };

    frame = (call_frame_t *)opaque;
    this = frame->this;
    local = frame->local;
    priv = this->private;
    query_child = local->read_txn_query_child;

    if (query_child == AFR_CHILD_ZERO) {
        possible_bad_child = AFR_CHILD_ONE;
    } else if (query_child == AFR_CHILD_ONE) {
        possible_bad_child = AFR_CHILD_ZERO;
    } else {
        /*read_txn_query_child is AFR_CHILD_UNKNOWN*/
        goto out;
    }

    /* Ask the query_child to see if it blames the possibly bad one. */
    xdata_req = dict_new();
    if (!xdata_req)
        goto out;

    pending = afr_matrix_create(priv->child_count, AFR_NUM_CHANGE_LOGS);
    if (!pending)
        goto out;

    ret = afr_set_pending_dict(priv, xdata_req, pending);
    if (ret < 0)
        goto out;

    if (local->fd) {
        ret = syncop_fxattrop(priv->children[query_child], local->fd,
                              GF_XATTROP_ADD_ARRAY, xdata_req, NULL, &xdata_rsp,
                              NULL);
    } else {
        ret = syncop_xattrop(priv->children[query_child], &local->loc,
                             GF_XATTROP_ADD_ARRAY, xdata_req, NULL, &xdata_rsp,
                             NULL);
    }
    if (ret || !xdata_rsp) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed xattrop for gfid %s on %s",
               uuid_utoa(local->inode->gfid),
               priv->children[query_child]->name);
        op_errno = -ret;
        goto out;
    }

    if (afr_ta_dict_contains_pending_xattr(xdata_rsp, priv,
                                           possible_bad_child)) {
        read_subvol = query_child;
        goto out;
    }
    dict_unref(xdata_rsp);
    xdata_rsp = NULL;

    /* It doesn't. So query thin-arbiter to see if it blames any data brick. */
    ret = afr_fill_ta_loc(this, &loc, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate thin-arbiter loc for: %s.", loc.name);
        goto out;
    }
    flock.l_type = F_WRLCK; /*start and length are already zero. */
    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_MODIFY, &loc, F_SETLKW, &flock, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "gfid:%s: Failed to get AFR_TA_DOM_MODIFY lock on %s.",
               uuid_utoa(local->inode->gfid),
               priv->pending_key[THIN_ARBITER_BRICK_INDEX]);
        op_errno = -ret;
        goto out;
    }

    ret = syncop_xattrop(priv->children[THIN_ARBITER_BRICK_INDEX], &loc,
                         GF_XATTROP_ADD_ARRAY, xdata_req, NULL, &xdata_rsp,
                         NULL);
    if (ret || !xdata_rsp) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "gfid:%s: Failed xattrop on %s.", uuid_utoa(local->inode->gfid),
               priv->pending_key[THIN_ARBITER_BRICK_INDEX]);
        op_errno = -ret;
        goto unlock;
    }

    if (!afr_ta_dict_contains_pending_xattr(xdata_rsp, priv, query_child)) {
        read_subvol = query_child;
    } else {
        gf_msg(this->name, GF_LOG_ERROR, EIO, AFR_MSG_THIN_ARB,
               "Failing read for gfid %s since good brick %s is down",
               uuid_utoa(local->inode->gfid),
               priv->children[possible_bad_child]->name);
        op_errno = EIO;
    }

unlock:
    flock.l_type = F_UNLCK;
    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_MODIFY, &loc, F_SETLK, &flock, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "gfid:%s: Failed to unlock AFR_TA_DOM_MODIFY lock on "
               "%s.",
               uuid_utoa(local->inode->gfid),
               priv->pending_key[THIN_ARBITER_BRICK_INDEX]);
    }
out:
    if (xdata_req)
        dict_unref(xdata_req);
    if (xdata_rsp)
        dict_unref(xdata_rsp);
    if (pending)
        afr_matrix_cleanup(pending, priv->child_count);
    loc_wipe(&loc);

    if (read_subvol == -1) {
        local->op_ret = -1;
        local->op_errno = op_errno;
    }
    afr_read_txn_wind(frame, this, read_subvol);
    return ret;
}

void
afr_ta_read_txn_synctask(call_frame_t *frame, xlator_t *this)
{
    call_frame_t *ta_frame = NULL;
    afr_local_t *local = NULL;
    int ret = 0;

    local = frame->local;
    ta_frame = afr_ta_frame_create(this);
    if (!ta_frame) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to create ta_frame");
        goto out;
    }
    ret = synctask_new(this->ctx->env, afr_ta_read_txn, afr_ta_read_txn_done,
                       ta_frame, frame);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to launch "
               "afr_ta_read_txn synctask for gfid %s.",
               uuid_utoa(local->inode->gfid));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        STACK_DESTROY(ta_frame->root);
        goto out;
    }
    return;
out:
    afr_read_txn_wind(frame, this, -1);
}

int
afr_read_txn_refresh_done(call_frame_t *frame, xlator_t *this, int err)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int read_subvol = -1;
    inode_t *inode = NULL;
    int ret = -1;
    int spb_subvol = -1;

    local = frame->local;
    inode = local->inode;
    priv = this->private;

    if (err) {
        if (!priv->thin_arbiter_count)
            goto readfn;
        if (err != EINVAL)
            goto readfn;
        /* We need to query the good bricks and/or thin-arbiter.*/
        afr_ta_read_txn_synctask(frame, this);
        return 0;
    }

    read_subvol = afr_read_subvol_select_by_policy(inode, this, local->readable,
                                                   NULL);
    if (read_subvol == -1) {
        err = EIO;
        goto readfn;
    }

    if (local->read_attempted[read_subvol]) {
        afr_read_txn_next_subvol(frame, this);
        return 0;
    }

    local->read_attempted[read_subvol] = 1;
readfn:
    if (read_subvol == -1) {
        ret = afr_split_brain_read_subvol_get(inode, this, frame, &spb_subvol);
        if ((ret == 0) && spb_subvol >= 0)
            read_subvol = spb_subvol;
    }

    if (read_subvol == -1) {
        AFR_SET_ERROR_AND_CHECK_SPLIT_BRAIN(-1, err);
    }
    afr_read_txn_wind(frame, this, read_subvol);

    return 0;
}

int
afr_read_txn_continue(call_frame_t *frame, xlator_t *this, int subvol)
{
    afr_local_t *local = NULL;

    local = frame->local;

    if (!local->refreshed) {
        local->refreshed = _gf_true;
        afr_inode_refresh(frame, this, local->inode, NULL,
                          afr_read_txn_refresh_done);
    } else {
        afr_read_txn_next_subvol(frame, this);
    }

    return 0;
}

/* afr_read_txn_wipe:

   clean internal variables in @local in order to make
   it possible to call afr_read_txn() multiple times from
   the same frame
*/

void
afr_read_txn_wipe(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;

    local = frame->local;
    priv = this->private;

    local->readfn = NULL;

    if (local->inode)
        inode_unref(local->inode);

    for (i = 0; i < priv->child_count; i++) {
        local->read_attempted[i] = 0;
        local->readable[i] = 0;
    }
}

/*
  afr_read_txn:

  This is the read transaction function. The way it works:

  - Determine read-subvolume from inode ctx.

  - If read-subvolume's generation was stale, refresh ctx once by
    calling afr_inode_refresh()

    Else make an attempt to read on read-subvolume.

  - If attempted read on read-subvolume fails, refresh ctx once
    by calling afr_inode_refresh()

  - After ctx refresh, query read-subvolume freshly and attempt
    read once.

  - If read fails, try every other readable[] subvolume before
    finally giving up. readable[] elements are set by afr_inode_refresh()
    based on dirty and pending flags.

  - If file is in split brain in the backend, generation will be
    kept 0 by afr_inode_refresh() and readable[] will be set 0 for
    all elements. Therefore reads always fail.
*/

int
afr_read_txn(call_frame_t *frame, xlator_t *this, inode_t *inode,
             afr_read_txn_wind_t readfn, afr_transaction_type type)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    unsigned char *data = NULL;
    unsigned char *metadata = NULL;
    int read_subvol = -1;
    int event_generation = 0;
    int ret = -1;

    priv = this->private;
    local = frame->local;
    data = alloca0(priv->child_count);
    metadata = alloca0(priv->child_count);

    afr_read_txn_wipe(frame, this);

    local->readfn = readfn;
    local->inode = inode_ref(inode);
    local->is_read_txn = _gf_true;
    local->transaction.type = type;

    if (priv->quorum_count && !afr_has_quorum(local->child_up, this, NULL)) {
        local->op_ret = -1;
        local->op_errno = afr_quorum_errno(priv);
        goto read;
    }

    if (!afr_is_consistent_io_possible(local, priv, &local->op_errno)) {
        local->op_ret = -1;
        goto read;
    }

    if (priv->thin_arbiter_count && !afr_ta_has_quorum(priv, local)) {
        local->op_ret = -1;
        local->op_errno = -afr_quorum_errno(priv);
        goto read;
    }

    if (priv->thin_arbiter_count &&
        AFR_COUNT(local->child_up, priv->child_count) != priv->child_count) {
        if (local->child_up[0]) {
            local->read_txn_query_child = AFR_CHILD_ZERO;
        } else if (local->child_up[1]) {
            local->read_txn_query_child = AFR_CHILD_ONE;
        }
        afr_ta_read_txn_synctask(frame, this);
        return 0;
    }

    ret = afr_inode_read_subvol_get(inode, this, data, metadata,
                                    &event_generation);
    if (ret == -1)
        /* very first transaction on this inode */
        goto refresh;
    AFR_INTERSECT(local->readable, data, metadata, priv->child_count);

    gf_msg_debug(this->name, 0,
                 "%s: generation now vs cached: %d, "
                 "%d",
                 uuid_utoa(inode->gfid), local->event_generation,
                 event_generation);
    if (afr_is_inode_refresh_reqd(inode, this, local->event_generation,
                                  event_generation))
        /* servers have disconnected / reconnected, and possibly
           rebooted, very likely changing the state of freshness
           of copies */
        goto refresh;

    read_subvol = afr_read_subvol_select_by_policy(inode, this, local->readable,
                                                   NULL);

    if (read_subvol < 0 || read_subvol > priv->child_count) {
        gf_msg_debug(this->name, 0,
                     "Unreadable subvolume %d found "
                     "with event generation %d for gfid %s.",
                     read_subvol, event_generation, uuid_utoa(inode->gfid));
        goto refresh;
    }

    if (!local->child_up[read_subvol]) {
        /* should never happen, just in case */
        gf_msg(this->name, GF_LOG_WARNING, 0, AFR_MSG_READ_SUBVOL_ERROR,
               "subvolume %d is the "
               "read subvolume in this generation, but is not up",
               read_subvol);
        goto refresh;
    }

    local->read_attempted[read_subvol] = 1;

read:
    afr_read_txn_wind(frame, this, read_subvol);

    return 0;

refresh:
    afr_inode_refresh(frame, this, inode, NULL, afr_read_txn_refresh_done);

    return 0;
}
