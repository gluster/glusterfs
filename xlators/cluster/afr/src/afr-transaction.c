/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dict.h"
#include "byte-order.h"
#include "common-utils.h"
#include "timer.h"

#include "afr.h"
#include "afr-transaction.h"

#include <signal.h>


#define LOCKED_NO       0x0        /* no lock held */
#define LOCKED_YES      0x1        /* for DATA, METADATA, ENTRY and higher_path
                                      of RENAME */
#define LOCKED_LOWER    0x2        /* for lower_path of RENAME */

afr_fd_ctx_t *
__afr_fd_ctx_get (fd_t *fd, xlator_t *this)
{
        uint64_t       ctx = 0;
        int            ret = 0;
        afr_fd_ctx_t  *fd_ctx = NULL;
        int            i = 0;
        afr_private_t *priv = NULL;

        priv = this->private;

        ret = __fd_ctx_get (fd, this, &ctx);

        if (ret < 0 && fd_is_anonymous (fd)) {
                ret = __afr_fd_ctx_set (this, fd);
                if (ret < 0)
                        goto out;

                ret = __fd_ctx_get (fd, this, &ctx);
                if (ret < 0)
                        goto out;

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;
                for (i = 0; i < priv->child_count; i++)
                        fd_ctx->opened_on[i] = AFR_FD_OPENED;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;
out:
        return fd_ctx;
}


afr_fd_ctx_t *
afr_fd_ctx_get (fd_t *fd, xlator_t *this)
{
        afr_fd_ctx_t  *fd_ctx = NULL;

        LOCK(&fd->lock);
        {
                fd_ctx = __afr_fd_ctx_get (fd, this);
        }
        UNLOCK(&fd->lock);

        return fd_ctx;
}


static void
afr_save_lk_owner (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

	local->saved_lk_owner = frame->root->lk_owner;
}


static void
afr_restore_lk_owner (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

	frame->root->lk_owner = local->saved_lk_owner;
}


static void
__mark_all_pending (int32_t *pending[], int child_count,
                    afr_transaction_type type)
{
        int i = 0;
        int j = 0;

        for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);
                pending[i][j] = hton32 (1);
        }
}


static void
__mark_child_dead (int32_t *pending[], int child_count, int child,
                   afr_transaction_type type)
{
        int j = 0;

        j = afr_index_for_transaction_type (type);

        pending[child][j] = 0;
}


static void
__mark_pre_op_done_on_fd (call_frame_t *frame, xlator_t *this, int child_index)
{
        afr_local_t   *local = NULL;
        afr_fd_ctx_t  *fd_ctx = NULL;

        local = frame->local;

        if (!local->fd)
                return;

        fd_ctx = afr_fd_ctx_get (local->fd, this);

        if (!fd_ctx)
                goto out;

        LOCK (&local->fd->lock);
        {
                if (local->transaction.type == AFR_DATA_TRANSACTION)
                        fd_ctx->pre_op_done[child_index]++;
        }
        UNLOCK (&local->fd->lock);
out:
        return;
}


static void
__mark_pre_op_undone_on_fd (call_frame_t *frame, xlator_t *this, int child_index)
{
        afr_local_t   *local = NULL;
        afr_fd_ctx_t  *fd_ctx = NULL;

        local = frame->local;

        if (!local->fd)
                return;

        fd_ctx = afr_fd_ctx_get (local->fd, this);

        if (!fd_ctx)
                goto out;

        LOCK (&local->fd->lock);
        {
                if (local->transaction.type == AFR_DATA_TRANSACTION) {
                        GF_ASSERT (fd_ctx->pre_op_done[child_index]);
                        fd_ctx->pre_op_done[child_index]--;
                }
        }
        UNLOCK (&local->fd->lock);
out:
        return;
}


static void
__mark_non_participant_children (int32_t *pending[], int child_count,
                                 unsigned char *participants,
                                 afr_transaction_type type)
{
        int i = 0;
        int j = 0;

        j = afr_index_for_transaction_type (type);
        for (i = 0; i < child_count; i++) {
                if (!participants[i])
                        pending[i][j] = 0;
        }
}


static void
__mark_all_success (int32_t *pending[], int child_count,
                    afr_transaction_type type)
{
        int i;
        int j;

        for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);
                pending[i][j] = hton32 (-1);
        }
}

void
_set_all_child_errno (int *child_errno, unsigned int child_count)
{
        int     i = 0;

        for (i = 0; i < child_count; i++)
                if (child_errno[i] == 0)
                        child_errno[i] = ENOTCONN;
}

void
afr_transaction_perform_fop (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_private_t   *priv = NULL;
        fd_t            *fd   = NULL;

        local = frame->local;
        priv  = this->private;
        fd    = local->fd;

        __mark_all_success (local->pending, priv->child_count,
                            local->transaction.type);

        _set_all_child_errno (local->child_errno, priv->child_count);

        /*  Perform fops with the lk-owner from top xlator.
         *  Eg: lk-owner of posix-lk and flush should be same,
         *  flush cant clear the  posix-lks without that lk-owner.
         */
        afr_save_lk_owner (frame);
        frame->root->lk_owner =
                local->transaction.main_frame->root->lk_owner;


        /* The wake up needs to happen independent of
           what type of fop arrives here. If it was
           a write, then it has already inherited the
           lock and changelog. If it was not a write,
           then the presumption of the optimization (of
           optimizing for successive write operations)
           fails.
        */
        if (fd)
                afr_delayed_changelog_wake_up (this, fd);
        local->transaction.fop (frame, this);
}


static int
__changelog_enabled (afr_private_t *priv, afr_transaction_type type)
{
        int ret = 0;

        switch (type) {
        case AFR_DATA_TRANSACTION:
                if (priv->data_change_log)
                        ret = 1;

                break;

        case AFR_METADATA_TRANSACTION:
                if (priv->metadata_change_log)
                        ret = 1;

                break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                if (priv->entry_change_log)
                        ret = 1;

                break;
        }

        return ret;
}


static int
__fop_changelog_needed (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        int op_ret = 0;
        afr_transaction_type type = -1;

        priv  = this->private;
        local = frame->local;
        type  = local->transaction.type;

        if (__changelog_enabled (priv, type)) {
                switch (local->op) {

                case GF_FOP_WRITE:
                case GF_FOP_FTRUNCATE:
                        op_ret = 1;
                        break;

                case GF_FOP_FLUSH:
                        op_ret = 0;
                        break;

                default:
                        op_ret = 1;
                }
        }

        return op_ret;
}

int
afr_set_pending_dict (afr_private_t *priv, dict_t *xattr, int32_t **pending,
                      int child, afr_xattrop_type_t op)
{
        int i = 0;
        int ret = 0;

        if (op == LOCAL_FIRST) {
                ret = dict_set_static_bin (xattr, priv->pending_key[child],
                                           pending[child],
                                   AFR_NUM_CHANGE_LOGS * sizeof (int32_t));
                if (ret)
                        goto out;
        }
        for (i = 0; i < priv->child_count; i++) {
                if (i == child)
                        continue;
                ret = dict_set_static_bin (xattr, priv->pending_key[i],
                                           pending[i],
                                   AFR_NUM_CHANGE_LOGS * sizeof (int32_t));
                /* 3 = data+metadata+entry */

                if (ret < 0)
                        goto out;
        }
        if (op == LOCAL_LAST) {
                ret = dict_set_static_bin (xattr, priv->pending_key[child],
                                           pending[child],
                                   AFR_NUM_CHANGE_LOGS * sizeof (int32_t));
                if (ret)
                        goto out;
        }
out:
        return ret;
}

int
afr_lock_server_count (afr_private_t *priv, afr_transaction_type type)
{
        int ret = 0;

        switch (type) {
        case AFR_DATA_TRANSACTION:
                ret = priv->child_count;
                break;

        case AFR_METADATA_TRANSACTION:
                ret = priv->child_count;
                break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                ret = priv->child_count;
                break;
        }

        return ret;
}

/* {{{ pending */

int32_t
afr_changelog_post_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xattr,
                           dict_t *xdata)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        afr_local_t         *local    = NULL;
        int                  call_count = -1;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                if (afr_lock_server_count (priv, local->transaction.type) == 0) {
                        local->transaction.done (frame, this);
                } else {
                        int_lock->lock_cbk = local->transaction.done;
                        afr_unlock (frame, this);
                }
        }

        return 0;
}


void
afr_transaction_rm_stale_children (call_frame_t *frame, xlator_t *this,
                                   inode_t *inode, afr_transaction_type type)
{
        int             i = -1;
        int             count = 0;
        int             read_child = -1;
        afr_private_t   *priv = NULL;
        afr_local_t     *local = NULL;
        int             **pending = NULL;
        int             idx = 0;
        int32_t         *stale_children = NULL;
        int32_t         *fresh_children = NULL;
        gf_boolean_t    rm_stale_children = _gf_false;

        idx = afr_index_for_transaction_type (type);

        priv = this->private;
        local = frame->local;
        pending = local->pending;

        if (local->op_ret < 0)
                goto out;
        fresh_children = local->fresh_children;
        read_child = afr_inode_get_read_ctx (this, inode, fresh_children);
        if (read_child < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "Possible split-brain "
                        "for %s", uuid_utoa (inode->gfid));
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!afr_is_child_present (fresh_children,
                                           priv->child_count, i))
                        continue;
                if (pending[i][idx])
                        continue;
                /* child is down or op failed on it */
                if (!stale_children)
                        stale_children = afr_children_create (priv->child_count);
                if (!stale_children)
                        goto out;

                rm_stale_children = _gf_true;
                stale_children[count++] = i;
                gf_log (this->name, GF_LOG_DEBUG, "Removing stale child "
                        "%d for %s", i, uuid_utoa (inode->gfid));
        }

        if (!rm_stale_children)
                goto out;

        afr_inode_rm_stale_children (this, inode, stale_children);
out:
        GF_FREE (stale_children);
        return;
}

unsigned char*
afr_locked_nodes_get (afr_transaction_type type, afr_internal_lock_t *int_lock)
{
        unsigned char *locked_nodes = NULL;
        switch (type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                locked_nodes = int_lock->inode_locked_nodes;
        break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                /*Because same set of subvols participate in all lockee
                 * entities*/
                locked_nodes = int_lock->lockee[0].locked_nodes;
        break;
        }
        return locked_nodes;
}

int
afr_changelog_pre_op_call_count (afr_transaction_type type,
                                 afr_internal_lock_t *int_lock,
                                 unsigned int child_count)
{
        int           call_count = 0;
        unsigned char *locked_nodes = NULL;

        locked_nodes = afr_locked_nodes_get (type, int_lock);
        GF_ASSERT (locked_nodes);

        call_count = afr_locked_children_count (locked_nodes, child_count);
        if (type == AFR_ENTRY_RENAME_TRANSACTION)
                call_count *= 2;

        return call_count;
}

int
afr_changelog_post_op_call_count (afr_transaction_type type,
                                  unsigned char *pre_op,
                                  unsigned int child_count)
{
        int           call_count = 0;

        call_count = afr_pre_op_done_children_count (pre_op, child_count);
        if (type == AFR_ENTRY_RENAME_TRANSACTION)
                call_count *= 2;

        return call_count;
}

void
afr_compute_txn_changelog (afr_local_t *local, afr_private_t *priv)
{
        int             i = 0;
        int             index = 0;
        int32_t         postop = 0;
        int32_t         preop = 1;
        int32_t         **txn_changelog = NULL;

        txn_changelog = local->transaction.txn_changelog;
        index = afr_index_for_transaction_type (local->transaction.type);
        for (i = 0; i < priv->child_count; i++) {
                postop = ntoh32 (local->pending[i][index]);
                txn_changelog[i][index] = hton32 (postop + preop);
        }
}

afr_xattrop_type_t
afr_get_postop_xattrop_type (int32_t **pending, int optimized, int child,
                             afr_transaction_type type)
{
        int                     index = 0;
        afr_xattrop_type_t      op = LOCAL_LAST;

        index = afr_index_for_transaction_type (type);
        if (optimized && !pending[child][index])
                op = LOCAL_FIRST;
        return op;
}

void
afr_set_postop_dict (afr_local_t *local, xlator_t *this, dict_t *xattr,
                     int optimized, int child)
{
        int32_t                 **txn_changelog = NULL;
        int32_t                 **changelog = NULL;
        afr_private_t           *priv = NULL;
        int                     ret = 0;
        afr_xattrop_type_t      op = LOCAL_LAST;

        priv = this->private;
        txn_changelog = local->transaction.txn_changelog;
        op = afr_get_postop_xattrop_type (local->pending, optimized, child,
                                          local->transaction.type);
        if (optimized)
                changelog = txn_changelog;
        else
                changelog = local->pending;
        ret = afr_set_pending_dict (priv, xattr, changelog, child, op);
        if (ret < 0)
                gf_log (this->name, GF_LOG_INFO,
                        "failed to set pending entry");
}

int
afr_changelog_post_op_now (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = this->private;
        afr_internal_lock_t *int_lock = NULL;
        int i          = 0;
        int call_count = 0;

        afr_local_t *  local = NULL;
        afr_fd_ctx_t  *fdctx = NULL;
        dict_t        **xattr = NULL;
        int            piggyback = 0;
        int            index = 0;
        int            nothing_failed = 1;

        local    = frame->local;
        int_lock = &local->internal_lock;

        __mark_non_participant_children (local->pending, priv->child_count,
                                         local->transaction.pre_op,
                                         local->transaction.type);

        if (local->fd)
                afr_transaction_rm_stale_children (frame, this,
                                                   local->fd->inode,
                                                   local->transaction.type);

        xattr = alloca (priv->child_count * sizeof (*xattr));
        memset (xattr, 0, (priv->child_count * sizeof (*xattr)));
        for (i = 0; i < priv->child_count; i++) {
                xattr[i] = dict_new ();
        }

        call_count = afr_changelog_post_op_call_count (local->transaction.type,
                                                       local->transaction.pre_op,
                                                       priv->child_count);
        local->call_count = call_count;

        if (local->fd)
                fdctx = afr_fd_ctx_get (local->fd, this);

        if (call_count == 0) {
                /* no child is up */
                int_lock->lock_cbk = local->transaction.done;
                afr_unlock (frame, this);
                goto out;
        }

        /* check if something has failed, to handle piggybacking */
        nothing_failed = 1;
        index = afr_index_for_transaction_type (local->transaction.type);
        for (i = 0; i < priv->child_count; i++) {
                if (local->pending[i][index] == 0) {
                        nothing_failed = 0;
                        break;
                }
        }

        afr_compute_txn_changelog (local , priv);

        for (i = 0; i < priv->child_count; i++) {
                if (!local->transaction.pre_op[i])
                        continue;

                if (local->transaction.type != AFR_DATA_TRANSACTION)
                        afr_set_postop_dict (local, this, xattr[i],
                                             local->optimistic_change_log, i);
                switch (local->transaction.type) {
                case AFR_DATA_TRANSACTION:
                {
                        if (!fdctx) {
                                afr_set_postop_dict (local, this, xattr[i],
                                                     0, i);
                                STACK_WIND (frame, afr_changelog_post_op_cbk,
                                            priv->children[i],
                                            priv->children[i]->fops->xattrop,
                                            &local->loc,
                                            GF_XATTROP_ADD_ARRAY, xattr[i],
                                            NULL);
                                break;
                        }

                        LOCK (&local->fd->lock);
                        {
                                piggyback = 0;
                                if (fdctx->pre_op_piggyback[i]) {
                                        fdctx->pre_op_piggyback[i]--;
                                        piggyback = 1;
                                }
                        }
                        UNLOCK (&local->fd->lock);

                        afr_set_postop_dict (local, this, xattr[i],
                                             piggyback, i);

                        if (nothing_failed && piggyback) {
                                afr_changelog_post_op_cbk (frame, (void *)(long)i,
                                                           this, 1, 0, xattr[i], NULL);
                        } else {
                                if (!piggyback)
                                        __mark_pre_op_undone_on_fd (frame, this,
                                                                    i);
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_post_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->fxattrop,
                                                   local->fd,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                        }
                }
                break;
                case AFR_METADATA_TRANSACTION:
                {
                        if (nothing_failed && local->optimistic_change_log) {
                                afr_changelog_post_op_cbk (frame, (void *)(long)i,
                                                           this, 1, 0, xattr[i],
                                                           NULL);
                                break;
                        }

                        if (local->fd)
                                STACK_WIND (frame, afr_changelog_post_op_cbk,
                                            priv->children[i],
                                            priv->children[i]->fops->fxattrop,
                                            local->fd,
                                            GF_XATTROP_ADD_ARRAY, xattr[i],
                                            NULL);
                        else
                                STACK_WIND (frame, afr_changelog_post_op_cbk,
                                            priv->children[i],
                                            priv->children[i]->fops->xattrop,
                                            &local->loc,
                                            GF_XATTROP_ADD_ARRAY, xattr[i],
                                            NULL);
                }
                break;

                case AFR_ENTRY_RENAME_TRANSACTION:
                {
                        if (nothing_failed && local->optimistic_change_log) {
                                afr_changelog_post_op_cbk (frame, (void *)(long)i,
                                                           this, 1, 0, xattr[i],
                                                           NULL);
                        } else {
                                STACK_WIND_COOKIE (frame, afr_changelog_post_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->xattrop,
                                                   &local->transaction.new_parent_loc,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                        }
                        call_count--;
                }

                /*
                  set it again because previous stack_wind
                  might have already returned (think of case
                  where subvolume is posix) and would have
                  used the dict as placeholder for return
                  value
                */

                afr_set_postop_dict (local, this, xattr[i],
                                     local->optimistic_change_log, i);

                /* fall through */

                case AFR_ENTRY_TRANSACTION:
                {
                        if (nothing_failed && local->optimistic_change_log) {
                                afr_changelog_post_op_cbk (frame, (void *)(long)i,
                                                           this, 1, 0, xattr[i],
                                                           NULL);
                                break;
                        }

                        if (local->fd)
                                STACK_WIND (frame, afr_changelog_post_op_cbk,
                                            priv->children[i],
                                            priv->children[i]->fops->fxattrop,
                                            local->fd,
                                            GF_XATTROP_ADD_ARRAY, xattr[i],
                                            NULL);
                        else
                                STACK_WIND (frame, afr_changelog_post_op_cbk,
                                            priv->children[i],
                                            priv->children[i]->fops->xattrop,
                                            &local->transaction.parent_loc,
                                            GF_XATTROP_ADD_ARRAY, xattr[i],
                                            NULL);
                }
                break;
                }

                if (!--call_count)
                        break;
        }

out:
        for (i = 0; i < priv->child_count; i++) {
                dict_unref (xattr[i]);
        }

        return 0;
}


int32_t
afr_changelog_pre_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xattr,
                          dict_t *xdata)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = this->private;
        int call_count  = -1;
        int child_index = (long) cookie;

        local = frame->local;

        LOCK (&frame->lock);
        {
                switch (op_ret) {
                case 0:
                        __mark_pre_op_done_on_fd (frame, this, child_index);
                        //fallthrough we need to mark the pre_op
                case 1:
                        local->transaction.pre_op[child_index] = 1;
                        /* special op_ret for piggyback */
                        break;
                case -1:
                        if (op_errno == ENOTSUP) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "xattrop not supported by %s",
                                        priv->children[child_index]->name);
                                local->op_ret = -1;

                        } else if (!child_went_down (op_ret, op_errno)) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "xattrop failed on child %s: %s",
                                        priv->children[child_index]->name,
                                        strerror (op_errno));
                        }
                        local->op_errno = op_errno;
                        break;
                }

                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                if ((local->op_ret == -1) &&
                    (local->op_errno == ENOTSUP)) {
                        local->transaction.resume (frame, this);
                } else {
                        afr_transaction_perform_fop (frame, this);
                }
        }

        return 0;
}

int
afr_changelog_pre_op (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = this->private;
        int i = 0;
        int ret = 0;
        int call_count = 0;
        dict_t **xattr = NULL;
        afr_fd_ctx_t *fdctx = NULL;
        afr_local_t *local = NULL;
        int          piggyback = 0;
        afr_internal_lock_t *int_lock = NULL;
        unsigned char       *locked_nodes = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;

        xattr = alloca (priv->child_count * sizeof (*xattr));
        memset (xattr, 0, (priv->child_count * sizeof (*xattr)));

        for (i = 0; i < priv->child_count; i++) {
                xattr[i] = dict_new ();
        }

        call_count = afr_changelog_pre_op_call_count (local->transaction.type,
                                                      int_lock,
                                                      priv->child_count);
        if (call_count == 0) {
                local->internal_lock.lock_cbk =
                        local->transaction.done;
                afr_unlock (frame, this);
                goto out;
        }

        local->call_count = call_count;

        __mark_all_pending (local->pending, priv->child_count,
                            local->transaction.type);

        if (local->fd)
                fdctx = afr_fd_ctx_get (local->fd, this);

        locked_nodes = afr_locked_nodes_get (local->transaction.type, int_lock);
        for (i = 0; i < priv->child_count; i++) {
                if (!locked_nodes[i])
                        continue;
                ret = afr_set_pending_dict (priv, xattr[i], local->pending,
                                            i, LOCAL_FIRST);

                if (ret < 0)
                        gf_log (this->name, GF_LOG_INFO,
                                "failed to set pending entry");


                switch (local->transaction.type) {
                case AFR_DATA_TRANSACTION:
                {
                        if (!fdctx) {
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->xattrop,
                                                   &(local->loc),
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                                break;
                        }

                        LOCK (&local->fd->lock);
                        {
                                piggyback = 0;
                                if (fdctx->pre_op_done[i]) {
                                        fdctx->pre_op_piggyback[i]++;
                                        piggyback = 1;
                                        fdctx->hit++;
                                } else {
                                        fdctx->miss++;
                                }
                        }
                        UNLOCK (&local->fd->lock);

			afr_set_delayed_post_op (frame, this);

                        if (piggyback)
                                afr_changelog_pre_op_cbk (frame, (void *)(long)i,
                                                          this, 1, 0, xattr[i],
                                                          NULL);
                        else
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->fxattrop,
                                                   local->fd,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                }
                break;
                case AFR_METADATA_TRANSACTION:
                {
                        if (local->optimistic_change_log) {
                                afr_changelog_pre_op_cbk (frame, (void *)(long)i,
                                                          this, 1, 0, xattr[i],
                                                          NULL);
                                break;
                        }

                        if (local->fd)
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->fxattrop,
                                                   local->fd,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                        else
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->xattrop,
                                                   &(local->loc),
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                }
                break;

                case AFR_ENTRY_RENAME_TRANSACTION:
                {
                        if (local->optimistic_change_log) {
                                afr_changelog_pre_op_cbk (frame, (void *)(long)i,
                                                          this, 1, 0, xattr[i],
                                                          NULL);
                        } else {
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->xattrop,
                                                   &local->transaction.new_parent_loc,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                        }

                        call_count--;
                }


                /*
                  set it again because previous stack_wind
                  might have already returned (think of case
                  where subvolume is posix) and would have
                  used the dict as placeholder for return
                  value
                */

                ret = afr_set_pending_dict (priv, xattr[i], local->pending,
                                            i, LOCAL_FIRST);

                if (ret < 0)
                        gf_log (this->name, GF_LOG_INFO,
                                "failed to set pending entry");

                /* fall through */

                case AFR_ENTRY_TRANSACTION:
                {
                        if (local->optimistic_change_log) {
                                afr_changelog_pre_op_cbk (frame, (void *)(long)i,
                                                          this, 1, 0, xattr[i],
                                                          NULL);
                                break;
                        }

                        if (local->fd)
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->fxattrop,
                                                   local->fd,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                        else
                                STACK_WIND_COOKIE (frame,
                                                   afr_changelog_pre_op_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->xattrop,
                                                   &local->transaction.parent_loc,
                                                   GF_XATTROP_ADD_ARRAY, xattr[i],
                                                   NULL);
                }
                break;
                }

                if (!--call_count)
                        break;
        }
out:
        for (i = 0; i < priv->child_count; i++) {
                dict_unref (xattr[i]);
        }

        return 0;
}


int
afr_post_blocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking inodelks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking inodelks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_nonblocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        /* Initiate blocking locks if non-blocking has failed */
        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking inodelks failed. Proceeding to blocking");
                int_lock->lock_cbk = afr_post_blocking_inodelk_cbk;
                afr_blocking_lock (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking inodelks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_blocking_entrylk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking entrylks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_nonblocking_entrylk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;

        /* Initiate blocking locks if non-blocking has failed */
        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking entrylks failed. Proceeding to blocking");
                int_lock->lock_cbk = afr_post_blocking_entrylk_cbk;
                afr_blocking_lock (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_blocking_rename_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking entrylks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }
        return 0;
}


int
afr_post_lower_unlock_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        GF_ASSERT (!int_lock->higher_locked);

        int_lock->lock_cbk = afr_post_blocking_rename_cbk;
        afr_blocking_lock (frame, this);

        return 0;
}


int
afr_set_transaction_flock (afr_local_t *local)
{
        afr_internal_lock_t *int_lock = NULL;

        int_lock = &local->internal_lock;

        int_lock->lk_flock.l_len   = local->transaction.len;
        int_lock->lk_flock.l_start = local->transaction.start;
        int_lock->lk_flock.l_type  = F_WRLCK;

        return 0;
}

int
afr_lock_rec (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_TRANSACTION_LK;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                afr_set_transaction_flock (local);

                int_lock->lock_cbk = afr_post_nonblocking_inodelk_cbk;

                afr_nonblocking_inodelk (frame, this);
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:

                int_lock->lock_cbk = afr_post_nonblocking_entrylk_cbk;
                afr_nonblocking_entrylk (frame, this);
                break;

        case AFR_ENTRY_TRANSACTION:
                int_lock->lk_basename = local->transaction.basename;
                if (&local->transaction.parent_loc)
                        int_lock->lk_loc = &local->transaction.parent_loc;
                else
                        GF_ASSERT (local->fd);

                int_lock->lock_cbk = afr_post_nonblocking_entrylk_cbk;
                afr_nonblocking_entrylk (frame, this);
                break;
        }

        return 0;
}


int
afr_lock (call_frame_t *frame, xlator_t *this)
{
        afr_set_lock_number (frame, this);

        return afr_lock_rec (frame, this);
}


/* }}} */

int
afr_internal_lock_finish (call_frame_t *frame, xlator_t *this)
{
        if (__fop_changelog_needed (frame, this)) {
                afr_changelog_pre_op (frame, this);
        } else {
                afr_transaction_perform_fop (frame, this);
        }

        return 0;
}


void
afr_set_delayed_post_op (call_frame_t *frame, xlator_t *this)
{
	afr_local_t    *local = NULL;
	afr_private_t  *priv = NULL;

	/* call this function from any of the related optimizations
	   which benefit from delaying post op are enabled, namely:

	   - changelog piggybacking
	   - eager locking
	*/

	priv = this->private;
	if (!priv)
		return;

	if (!priv->post_op_delay_secs)
		return;

        local = frame->local;
        if (!local->transaction.eager_lock_on)
                return;

	if (!local)
		return;

	if (!local->fd)
		return;

	if (local->op == GF_FOP_WRITE)
		local->delayed_post_op = _gf_true;
}


gf_boolean_t
is_afr_delayed_changelog_post_op_needed (call_frame_t *frame, xlator_t *this)
{
	afr_local_t      *local = NULL;
	gf_boolean_t      res = _gf_false;

	local = frame->local;
	if (!local)
		goto out;

	if (!local->delayed_post_op)
		goto out;

	res = _gf_true;
out:
	return res;
}


void
afr_delayed_changelog_post_op (xlator_t *this, call_frame_t *frame, fd_t *fd);

void
afr_delayed_changelog_wake_up_cbk (void *data)
{
	fd_t           *fd = NULL;

	fd = data;

	afr_delayed_changelog_wake_up (THIS, fd);
}


void
afr_delayed_changelog_post_op (xlator_t *this, call_frame_t *frame, fd_t *fd)
{
	afr_fd_ctx_t      *fd_ctx = NULL;
	call_frame_t      *prev_frame = NULL;
	struct timeval     delta = {0, };
	afr_private_t     *priv = NULL;

	priv = this->private;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		return;

	delta.tv_sec = priv->post_op_delay_secs;
	delta.tv_usec = 0;

	pthread_mutex_lock (&fd_ctx->delay_lock);
	{
		prev_frame = fd_ctx->delay_frame;
		fd_ctx->delay_frame = NULL;
		if (fd_ctx->delay_timer)
			gf_timer_call_cancel (this->ctx, fd_ctx->delay_timer);
		fd_ctx->delay_timer = NULL;
		if (!frame)
			goto unlock;
		fd_ctx->delay_timer = gf_timer_call_after (this->ctx, delta,
							   afr_delayed_changelog_wake_up_cbk,
							   fd);
		fd_ctx->delay_frame = frame;
	}
unlock:
	pthread_mutex_unlock (&fd_ctx->delay_lock);

	if (prev_frame) {
		afr_changelog_post_op_now (prev_frame, this);
	}
}


void
afr_changelog_post_op (call_frame_t *frame, xlator_t *this)
{
	afr_local_t  *local = NULL;

	local = frame->local;

	if (is_afr_delayed_changelog_post_op_needed (frame, this))
		afr_delayed_changelog_post_op (this, frame, local->fd);
	else
		afr_changelog_post_op_now (frame, this);
}


void
afr_delayed_changelog_wake_up (xlator_t *this, fd_t *fd)
{
	afr_delayed_changelog_post_op (this, NULL, fd);
}


int
afr_transaction_resume (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

	afr_restore_lk_owner (frame);

        if (__fop_changelog_needed (frame, this)) {
                afr_changelog_post_op (frame, this);
        } else {
                if (afr_lock_server_count (priv, local->transaction.type) == 0) {
                        local->transaction.done (frame, this);
                } else {
                        int_lock->lock_cbk = local->transaction.done;
                        afr_unlock (frame, this);
                }
        }

        return 0;
}


/**
 * afr_transaction_fop_failed - inform that an fop failed
 */

void
afr_transaction_fop_failed (call_frame_t *frame, xlator_t *this, int child_index)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;

        local = frame->local;
        priv  = this->private;

        __mark_child_dead (local->pending, priv->child_count,
                           child_index, local->transaction.type);
}

int
afr_transaction (call_frame_t *frame, xlator_t *this, afr_transaction_type type)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        fd_t            *fd   = NULL;
        int             ret   = -1;

        local = frame->local;
        priv  = this->private;

        local->transaction.resume = afr_transaction_resume;
        local->transaction.type   = type;

        ret = afr_transaction_local_init (local, this);
        if (ret < 0) {
            goto out;
        }

        if (local->fd && local->transaction.eager_lock_on)
                afr_set_lk_owner (frame, this, local->fd);
        else
                afr_set_lk_owner (frame, this, frame->root);

        if (!local->transaction.eager_lock_on && local->loc.inode) {
                fd = fd_lookup (local->loc.inode, frame->root->pid);
                if (fd == NULL)
                        fd = fd_lookup_anonymous (local->loc.inode);

                if (fd) {
                        afr_delayed_changelog_wake_up (this, fd);
                        fd_unref (fd);
                }
        }

        if (afr_lock_server_count (priv, local->transaction.type) == 0) {
                afr_internal_lock_finish (frame, this);
        } else {
                afr_lock (frame, this);
        }
        ret = 0;
out:
        return ret;
}
