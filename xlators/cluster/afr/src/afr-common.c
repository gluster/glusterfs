/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#include <glusterfs/glusterfs.h>
#include "afr.h"
#include <glusterfs/dict.h>
#include <glusterfs/hashfn.h>
#include <glusterfs/list.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/compat.h>
#include <glusterfs/byte-order.h>
#include <glusterfs/statedump.h>
#include <glusterfs/events.h>
#include <glusterfs/upcall-utils.h>

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heald.h"
#include "afr-messages.h"

int32_t
afr_quorum_errno(afr_private_t *priv)
{
    return ENOTCONN;
}

gf_boolean_t
afr_is_private_directory(afr_private_t *priv, uuid_t pargfid, const char *name,
                         pid_t pid)
{
    if (!__is_root_gfid(pargfid)) {
        return _gf_false;
    }

    if (strcmp(name, GF_REPLICATE_TRASH_DIR) == 0) {
        /*For backward compatibility /.landfill is private*/
        return _gf_true;
    }

    if (pid == GF_CLIENT_PID_GSYNCD) {
        /*geo-rep needs to create/sync private directory on secondary because
         * it appears in changelog*/
        return _gf_false;
    }

    if (pid == GF_CLIENT_PID_GLFS_HEAL || pid == GF_CLIENT_PID_SELF_HEALD) {
        if (strcmp(name, priv->anon_inode_name) == 0) {
            /* anonymous-inode dir is private*/
            return _gf_true;
        }
    } else {
        if (strncmp(name, AFR_ANON_DIR_PREFIX, strlen(AFR_ANON_DIR_PREFIX)) ==
            0) {
            /* anonymous-inode dir prefix is private for geo-rep to work*/
            return _gf_true;
        }
    }

    return _gf_false;
}

void
afr_fill_success_replies(afr_local_t *local, afr_private_t *priv,
                         unsigned char *replies)
{
    int i = 0;

    for (i = 0; i < priv->child_count; i++) {
        if (local->replies[i].valid && local->replies[i].op_ret == 0) {
            replies[i] = 1;
        } else {
            replies[i] = 0;
        }
    }
}

int
afr_fav_child_reset_sink_xattrs(void *opaque);

int
afr_fav_child_reset_sink_xattrs_cbk(int ret, call_frame_t *frame, void *opaque);

static void
afr_discover_done(call_frame_t *frame, xlator_t *this);

int
afr_dom_lock_acquire_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = this->private;
    int i = (long)cookie;

    local->cont.lk.dom_lock_op_ret[i] = op_ret;
    local->cont.lk.dom_lock_op_errno[i] = op_errno;
    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_LK_HEAL_DOM,
               "%s: Failed to acquire %s on %s",
               uuid_utoa(local->fd->inode->gfid), AFR_LK_HEAL_DOM,
               priv->children[i]->name);
    } else {
        local->cont.lk.dom_locked_nodes[i] = 1;
    }

    syncbarrier_wake(&local->barrier);

    return 0;
}

int
afr_dom_lock_acquire(call_frame_t *frame)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    struct gf_flock flock = {
        0,
    };
    int i = 0;

    priv = frame->this->private;
    local = frame->local;
    local->cont.lk.dom_locked_nodes = GF_CALLOC(
        priv->child_count, sizeof(*local->cont.lk.locked_nodes),
        gf_afr_mt_char);
    if (!local->cont.lk.dom_locked_nodes) {
        return -ENOMEM;
    }
    local->cont.lk.dom_lock_op_ret = GF_CALLOC(
        priv->child_count, sizeof(*local->cont.lk.dom_lock_op_ret),
        gf_afr_mt_int32_t);
    if (!local->cont.lk.dom_lock_op_ret) {
        return -ENOMEM; /* CALLOC'd members are freed in afr_local_cleanup. */
    }
    local->cont.lk.dom_lock_op_errno = GF_CALLOC(
        priv->child_count, sizeof(*local->cont.lk.dom_lock_op_errno),
        gf_afr_mt_int32_t);
    if (!local->cont.lk.dom_lock_op_errno) {
        return -ENOMEM; /* CALLOC'd members are freed in afr_local_cleanup. */
    }
    flock.l_type = F_WRLCK;

    AFR_ONALL(frame, afr_dom_lock_acquire_cbk, finodelk, AFR_LK_HEAL_DOM,
              local->fd, F_SETLK, &flock, NULL);

    if (!afr_has_quorum(local->cont.lk.dom_locked_nodes, frame->this, NULL))
        goto blocking_lock;

    /*If any of the bricks returned EAGAIN, we still need blocking locks.*/
    if (AFR_COUNT(local->cont.lk.dom_locked_nodes, priv->child_count) !=
        priv->child_count) {
        for (i = 0; i < priv->child_count; i++) {
            if (local->cont.lk.dom_lock_op_ret[i] == -1 &&
                local->cont.lk.dom_lock_op_errno[i] == EAGAIN)
                goto blocking_lock;
        }
    }

    return 0;

blocking_lock:
    afr_dom_lock_release(frame);
    AFR_ONALL(frame, afr_dom_lock_acquire_cbk, finodelk, AFR_LK_HEAL_DOM,
              local->fd, F_SETLKW, &flock, NULL);
    if (!afr_has_quorum(local->cont.lk.dom_locked_nodes, frame->this, NULL)) {
        afr_dom_lock_release(frame);
        return -afr_quorum_errno(priv);
    }

    return 0;
}

int
afr_dom_lock_release_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = this->private;
    int i = (long)cookie;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_LK_HEAL_DOM,
               "%s: Failed to release %s on %s", local->loc.path,
               AFR_LK_HEAL_DOM, priv->children[i]->name);
    }
    local->cont.lk.dom_locked_nodes[i] = 0;

    syncbarrier_wake(&local->barrier);

    return 0;
}

void
afr_dom_lock_release(call_frame_t *frame)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    unsigned char *locked_on = NULL;
    struct gf_flock flock = {
        0,
    };

    local = frame->local;
    priv = frame->this->private;
    locked_on = local->cont.lk.dom_locked_nodes;
    if (AFR_COUNT(locked_on, priv->child_count) == 0)
        return;
    flock.l_type = F_UNLCK;

    AFR_ONLIST(locked_on, frame, afr_dom_lock_release_cbk, finodelk,
               AFR_LK_HEAL_DOM, local->fd, F_SETLK, &flock, NULL);

    return;
}

static void
afr_lk_heal_info_cleanup(afr_lk_heal_info_t *info)
{
    if (!info)
        return;
    if (info->xdata_req)
        dict_unref(info->xdata_req);
    if (info->fd)
        fd_unref(info->fd);
    GF_FREE(info->locked_nodes);
    GF_FREE(info->child_up_event_gen);
    GF_FREE(info->child_down_event_gen);
    GF_FREE(info);
}

static int
afr_add_lock_to_saved_locks(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = this->private;
    afr_local_t *local = frame->local;
    afr_lk_heal_info_t *info = NULL;
    afr_fd_ctx_t *fd_ctx = NULL;
    int ret = -ENOMEM;

    info = GF_CALLOC(sizeof(*info), 1, gf_afr_mt_lk_heal_info_t);
    if (!info) {
        goto cleanup;
    }
    INIT_LIST_HEAD(&info->pos);
    info->fd = fd_ref(local->fd);
    info->cmd = local->cont.lk.cmd;
    info->pid = frame->root->pid;
    info->flock = local->cont.lk.user_flock;
    info->xdata_req = dict_copy_with_ref(local->xdata_req, NULL);
    if (!info->xdata_req) {
        goto cleanup;
    }
    info->lk_owner = frame->root->lk_owner;
    info->locked_nodes = GF_MALLOC(
        sizeof(*info->locked_nodes) * priv->child_count, gf_afr_mt_char);
    if (!info->locked_nodes) {
        goto cleanup;
    }
    memcpy(info->locked_nodes, local->cont.lk.locked_nodes,
           sizeof(*info->locked_nodes) * priv->child_count);
    info->child_up_event_gen = GF_CALLOC(sizeof(*info->child_up_event_gen),
                                         priv->child_count, gf_afr_mt_int32_t);
    if (!info->child_up_event_gen) {
        goto cleanup;
    }
    info->child_down_event_gen = GF_CALLOC(sizeof(*info->child_down_event_gen),
                                           priv->child_count,
                                           gf_afr_mt_int32_t);
    if (!info->child_down_event_gen) {
        goto cleanup;
    }

    LOCK(&local->fd->lock);
    {
        fd_ctx = __afr_fd_ctx_get(local->fd, this);
        if (fd_ctx)
            fd_ctx->lk_heal_info = info;
    }
    UNLOCK(&local->fd->lock);
    if (!fd_ctx) {
        goto cleanup;
    }

    LOCK(&priv->lock);
    {
        list_add_tail(&info->pos, &priv->saved_locks);
    }
    UNLOCK(&priv->lock);

    return 0;
cleanup:
    gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_LK_HEAL_DOM,
           "%s: Failed to add lock to healq",
           uuid_utoa(local->fd->inode->gfid));
    if (info) {
        afr_lk_heal_info_cleanup(info);
        if (fd_ctx) {
            LOCK(&local->fd->lock);
            {
                fd_ctx->lk_heal_info = NULL;
            }
            UNLOCK(&local->fd->lock);
        }
    }
    return ret;
}

static int
afr_remove_lock_from_saved_locks(afr_local_t *local, xlator_t *this)
{
    afr_private_t *priv = this->private;
    struct gf_flock flock = local->cont.lk.user_flock;
    afr_lk_heal_info_t *info = NULL;
    afr_fd_ctx_t *fd_ctx = NULL;
    int ret = -EINVAL;

    fd_ctx = afr_fd_ctx_get(local->fd, this);
    if (!fd_ctx || !fd_ctx->lk_heal_info) {
        goto out;
    }

    info = fd_ctx->lk_heal_info;
    if ((info->flock.l_start != flock.l_start) ||
        (info->flock.l_whence != flock.l_whence) ||
        (info->flock.l_len != flock.l_len)) {
        /*TODO: Compare lkowners too.*/
        goto out;
    }

    LOCK(&priv->lock);
    {
        list_del(&fd_ctx->lk_heal_info->pos);
    }
    UNLOCK(&priv->lock);

    afr_lk_heal_info_cleanup(info);
    fd_ctx->lk_heal_info = NULL;
    ret = 0;
out:
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_LK_HEAL_DOM,
               "%s: Failed to remove lock from healq",
               uuid_utoa(local->fd->inode->gfid));
    return ret;
}

int
afr_lock_heal_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                  dict_t *xdata)
{
    afr_local_t *local = frame->local;
    int i = (long)cookie;

    local->replies[i].valid = 1;
    local->replies[i].op_ret = op_ret;
    local->replies[i].op_errno = op_errno;
    if (op_ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_LK_HEAL_DOM,
               "Failed to heal lock on child %d for %s", i,
               uuid_utoa(local->fd->inode->gfid));
    }
    syncbarrier_wake(&local->barrier);
    return 0;
}

int
afr_getlk_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
    afr_local_t *local = frame->local;
    int i = (long)cookie;

    local->replies[i].valid = 1;
    local->replies[i].op_ret = op_ret;
    local->replies[i].op_errno = op_errno;
    if (op_ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_LK_HEAL_DOM,
               "Failed getlk for %s", uuid_utoa(local->fd->inode->gfid));
    } else {
        local->cont.lk.getlk_rsp[i] = *lock;
    }

    syncbarrier_wake(&local->barrier);
    return 0;
}

static gf_boolean_t
afr_does_lk_owner_match(call_frame_t *frame, afr_private_t *priv,
                        afr_lk_heal_info_t *info)
{
    int i = 0;
    afr_local_t *local = frame->local;
    struct gf_flock flock = {
        0,
    };
    gf_boolean_t ret = _gf_true;
    char *wind_on = alloca0(priv->child_count);
    unsigned char *success_replies = alloca0(priv->child_count);
    local->cont.lk.getlk_rsp = GF_CALLOC(sizeof(*local->cont.lk.getlk_rsp),
                                         priv->child_count, gf_afr_mt_gf_lock);

    flock = info->flock;
    for (i = 0; i < priv->child_count; i++) {
        if (info->locked_nodes[i])
            wind_on[i] = 1;
    }

    AFR_ONLIST(wind_on, frame, afr_getlk_cbk, lk, info->fd, F_GETLK, &flock,
               info->xdata_req);

    afr_fill_success_replies(local, priv, success_replies);
    if (AFR_COUNT(success_replies, priv->child_count) == 0) {
        ret = _gf_false;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid || local->replies[i].op_ret != 0)
            continue;
        if (local->cont.lk.getlk_rsp[i].l_type == F_UNLCK)
            continue;
        /*TODO: Do we really need to compare lkowner if F_UNLCK is true?*/
        if (!is_same_lkowner(&local->cont.lk.getlk_rsp[i].l_owner,
                             &info->lk_owner)) {
            ret = _gf_false;
            break;
        }
    }
out:
    afr_local_replies_wipe(local, priv);
    GF_FREE(local->cont.lk.getlk_rsp);
    local->cont.lk.getlk_rsp = NULL;
    return ret;
}

static void
afr_mark_fd_bad(fd_t *fd, xlator_t *this)
{
    afr_fd_ctx_t *fd_ctx = NULL;

    if (!fd)
        return;
    LOCK(&fd->lock);
    {
        fd_ctx = __afr_fd_ctx_get(fd, this);
        if (fd_ctx) {
            fd_ctx->is_fd_bad = _gf_true;
            fd_ctx->lk_heal_info = NULL;
        }
    }
    UNLOCK(&fd->lock);
}

static void
afr_add_lock_to_lkhealq(afr_private_t *priv, afr_lk_heal_info_t *info)
{
    LOCK(&priv->lock);
    {
        list_del(&info->pos);
        list_add_tail(&info->pos, &priv->lk_healq);
    }
    UNLOCK(&priv->lock);
}

static void
afr_lock_heal_do(call_frame_t *frame, afr_private_t *priv,
                 afr_lk_heal_info_t *info)
{
    int i = 0;
    int op_errno = 0;
    int32_t *current_event_gen = NULL;
    afr_local_t *local = frame->local;
    xlator_t *this = frame->this;
    char *wind_on = alloca0(priv->child_count);
    gf_boolean_t retry = _gf_true;

    frame->root->pid = info->pid;
    lk_owner_copy(&frame->root->lk_owner, &info->lk_owner);

    op_errno = -afr_dom_lock_acquire(frame);
    if ((op_errno != 0)) {
        goto release;
    }

    if (!afr_does_lk_owner_match(frame, priv, info)) {
        gf_msg(this->name, GF_LOG_WARNING, 0, AFR_MSG_LK_HEAL_DOM,
               "Ignoring lock heal for %s since lk-onwers mismatch. "
               "Lock possibly pre-empted by another client.",
               uuid_utoa(info->fd->inode->gfid));
        goto release;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (info->locked_nodes[i])
            continue;
        wind_on[i] = 1;
    }

    current_event_gen = alloca(priv->child_count);
    memcpy(current_event_gen, info->child_up_event_gen,
           priv->child_count * sizeof *current_event_gen);
    AFR_ONLIST(wind_on, frame, afr_lock_heal_cbk, lk, info->fd, info->cmd,
               &info->flock, info->xdata_req);

    LOCK(&priv->lock);
    {
        for (i = 0; i < priv->child_count; i++) {
            if (!wind_on[i])
                continue;
            if ((!local->replies[i].valid) || (local->replies[i].op_ret != 0)) {
                continue;
            }

            if ((current_event_gen[i] == info->child_up_event_gen[i]) &&
                (current_event_gen[i] > info->child_down_event_gen[i])) {
                info->locked_nodes[i] = 1;
                retry = _gf_false;
                list_del_init(&info->pos);
                list_add_tail(&info->pos, &priv->saved_locks);
            } else {
                /*We received subsequent child up/down events while heal was in
                 * progress; don't mark child as healed. Attempt again on the
                 * new child up*/
                gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_LK_HEAL_DOM,
                       "Event gen mismatch: skipped healing lock on child %d "
                       "for %s.",
                       i, uuid_utoa(info->fd->inode->gfid));
            }
        }
    }
    UNLOCK(&priv->lock);

release:
    afr_dom_lock_release(frame);
    if (retry)
        afr_add_lock_to_lkhealq(priv, info);
    return;
}

static int
afr_lock_heal_done(int ret, call_frame_t *frame, void *opaque)
{
    STACK_DESTROY(frame->root);
    return 0;
}

static int
afr_lock_heal(void *opaque)
{
    call_frame_t *frame = (call_frame_t *)opaque;
    call_frame_t *iter_frame = NULL;
    xlator_t *this = frame->this;
    afr_private_t *priv = this->private;
    afr_lk_heal_info_t *info = NULL;
    afr_lk_heal_info_t *tmp = NULL;
    struct list_head healq = {
        0,
    };
    int ret = 0;

    iter_frame = afr_copy_frame(frame);
    if (!iter_frame) {
        return ENOMEM;
    }

    INIT_LIST_HEAD(&healq);
    LOCK(&priv->lock);
    {
        list_splice_init(&priv->lk_healq, &healq);
    }
    UNLOCK(&priv->lock);

    list_for_each_entry_safe(info, tmp, &healq, pos)
    {
        GF_ASSERT((AFR_COUNT(info->locked_nodes, priv->child_count) <
                   priv->child_count));
        ((afr_local_t *)(iter_frame->local))->fd = fd_ref(info->fd);
        afr_lock_heal_do(iter_frame, priv, info);
        AFR_STACK_RESET(iter_frame);
        if (iter_frame->local == NULL) {
            ret = ENOTCONN;
            gf_msg(frame->this->name, GF_LOG_ERROR, ENOTCONN,
                   AFR_MSG_LK_HEAL_DOM,
                   "Aborting processing of lk_healq."
                   "Healing will be reattempted on next child up for locks "
                   "that are still in quorum.");
            LOCK(&priv->lock);
            {
                list_add_tail(&healq, &priv->lk_healq);
            }
            UNLOCK(&priv->lock);
            break;
        }
    }

    AFR_STACK_DESTROY(iter_frame);
    return ret;
}

static int
__afr_lock_heal_synctask(xlator_t *this, afr_private_t *priv, int child)
{
    int ret = 0;
    call_frame_t *frame = NULL;
    afr_lk_heal_info_t *info = NULL;
    afr_lk_heal_info_t *tmp = NULL;

    if (priv->shd.iamshd)
        return 0;

    list_for_each_entry_safe(info, tmp, &priv->saved_locks, pos)
    {
        info->child_up_event_gen[child] = priv->event_generation;
        list_del_init(&info->pos);
        list_add_tail(&info->pos, &priv->lk_healq);
    }

    frame = create_frame(this, this->ctx->pool);
    if (!frame)
        return -1;

    ret = synctask_new(this->ctx->env, afr_lock_heal, afr_lock_heal_done, frame,
                       frame);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_LK_HEAL_DOM,
               "Failed to launch lock heal synctask");

    return ret;
}

static int
__afr_mark_pending_lk_heal(xlator_t *this, afr_private_t *priv, int child)
{
    afr_lk_heal_info_t *info = NULL;
    afr_lk_heal_info_t *tmp = NULL;

    if (priv->shd.iamshd)
        return 0;
    list_for_each_entry_safe(info, tmp, &priv->saved_locks, pos)
    {
        info->child_down_event_gen[child] = priv->event_generation;
        if (info->locked_nodes[child] == 1)
            info->locked_nodes[child] = 0;
        if (!afr_has_quorum(info->locked_nodes, this, NULL)) {
            /* Since the lock was lost on quorum no. of nodes, we should
             * not attempt to heal it anymore. Some other client could have
             * acquired the lock, modified data and released it and this
             * client wouldn't know about it if we heal it.*/
            afr_mark_fd_bad(info->fd, this);
            list_del(&info->pos);
            afr_lk_heal_info_cleanup(info);
            /* We're not winding an unlock on the node where the lock is still
             * present because when fencing logic switches over to the new
             * client (since we marked the fd bad), it should preempt any
             * existing lock. */
        }
    }
    return 0;
}

gf_boolean_t
afr_is_consistent_io_possible(afr_local_t *local, afr_private_t *priv,
                              int32_t *op_errno)
{
    if (priv->consistent_io && local->call_count != priv->child_count) {
        gf_msg(THIS->name, GF_LOG_INFO, 0, AFR_MSG_SUBVOLS_DOWN,
               "All subvolumes are not up");
        if (op_errno)
            *op_errno = ENOTCONN;
        return _gf_false;
    }
    return _gf_true;
}

gf_boolean_t
afr_is_lock_mode_mandatory(dict_t *xdata)
{
    int ret = 0;
    uint32_t lk_mode = GF_LK_ADVISORY;

    ret = dict_get_uint32(xdata, GF_LOCK_MODE, &lk_mode);
    if (!ret && lk_mode == GF_LK_MANDATORY)
        return _gf_true;

    return _gf_false;
}

call_frame_t *
afr_copy_frame(call_frame_t *base)
{
    afr_local_t *local = NULL;
    call_frame_t *frame = NULL;
    int op_errno = 0;

    frame = copy_frame(base);
    if (!frame)
        return NULL;
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local) {
        AFR_STACK_DESTROY(frame);
        return NULL;
    }

    return frame;
}

/* Check if an entry or inode could be undergoing a transaction. */
gf_boolean_t
afr_is_possibly_under_txn(afr_transaction_type type, afr_local_t *local,
                          xlator_t *this)
{
    int i = 0;
    int tmp = 0;
    afr_private_t *priv = NULL;
    GF_UNUSED char *key = NULL;
    int keylen = 0;

    priv = this->private;

    if (type == AFR_ENTRY_TRANSACTION) {
        key = GLUSTERFS_PARENT_ENTRYLK;
        keylen = SLEN(GLUSTERFS_PARENT_ENTRYLK);
    } else if (type == AFR_DATA_TRANSACTION) {
        /*FIXME: Use GLUSTERFS_INODELK_DOM_COUNT etc. once
         * pl_inodelk_xattr_fill supports separate keys for different
         * domains.*/
        key = GLUSTERFS_INODELK_COUNT;
        keylen = SLEN(GLUSTERFS_INODELK_COUNT);
    }
    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].xdata)
            continue;
        if (dict_get_int32n(local->replies[i].xdata, key, keylen, &tmp) == 0)
            if (tmp)
                return _gf_true;
    }

    return _gf_false;
}

static void
afr_inode_ctx_destroy(afr_inode_ctx_t *ctx)
{
    int i = 0;

    if (!ctx)
        return;

    for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++) {
        GF_FREE(ctx->pre_op_done[i]);
    }

    GF_FREE(ctx);
}

int
__afr_inode_ctx_get(xlator_t *this, inode_t *inode, afr_inode_ctx_t **ctx)
{
    uint64_t ctx_int = 0;
    int ret = -1;
    int i = -1;
    int num_locks = -1;
    afr_inode_ctx_t *ictx = NULL;
    afr_lock_t *lock = NULL;
    afr_private_t *priv = this->private;

    ret = __inode_ctx_get(inode, this, &ctx_int);
    if (ret == 0) {
        *ctx = (afr_inode_ctx_t *)(uintptr_t)ctx_int;
        return 0;
    }

    ictx = GF_CALLOC(1, sizeof(afr_inode_ctx_t), gf_afr_mt_inode_ctx_t);
    if (!ictx)
        goto out;

    for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++) {
        ictx->pre_op_done[i] = GF_CALLOC(sizeof *ictx->pre_op_done[i],
                                         priv->child_count, gf_afr_mt_int32_t);
        if (!ictx->pre_op_done[i]) {
            ret = -ENOMEM;
            goto out;
        }
    }

    num_locks = sizeof(ictx->lock) / sizeof(afr_lock_t);
    for (i = 0; i < num_locks; i++) {
        lock = &ictx->lock[i];
        INIT_LIST_HEAD(&lock->post_op);
        INIT_LIST_HEAD(&lock->frozen);
        INIT_LIST_HEAD(&lock->waiting);
        INIT_LIST_HEAD(&lock->owners);
    }

    ctx_int = (uint64_t)(uintptr_t)ictx;
    ret = __inode_ctx_set(inode, this, &ctx_int);
    if (ret) {
        goto out;
    }

    ictx->spb_choice = -1;
    ictx->read_subvol = 0;
    ictx->write_subvol = 0;
    ictx->lock_count = 0;
    ret = 0;
    *ctx = ictx;
out:
    if (ret) {
        afr_inode_ctx_destroy(ictx);
    }
    return ret;
}

/*
 * INODE CTX 64-bit VALUE FORMAT FOR SMALL (<= 16) SUBVOL COUNTS:
 *
 * |<----------   64bit   ------------>|
 *  63           32 31    16 15       0
 * |   EVENT_GEN   |  DATA  | METADATA |
 *
 *
 *  METADATA (bit-0 .. bit-15): bitmap representing subvolumes from which
 *                              metadata can be attempted to be read.
 *
 *                              bit-0 => priv->subvolumes[0]
 *                              bit-1 => priv->subvolumes[1]
 *                              ... etc. till bit-15
 *
 *  DATA (bit-16 .. bit-31): bitmap representing subvolumes from which data
 *                           can be attempted to be read.
 *
 *                           bit-16 => priv->subvolumes[0]
 *                           bit-17 => priv->subvolumes[1]
 *                           ... etc. till bit-31
 *
 *  EVENT_GEN (bit-32 .. bit-63): event generation (i.e priv->event_generation)
 *                                when DATA and METADATA was last updated.
 *
 *                                If EVENT_GEN is < priv->event_generation,
 *                                or is 0, it means afr_inode_refresh() needs
 *                                to be called to recalculate the bitmaps.
 */

int
__afr_set_in_flight_sb_status(xlator_t *this, afr_local_t *local,
                              inode_t *inode)
{
    int i = 0;
    int txn_type = 0;
    int count = 0;
    int index = -1;
    uint16_t datamap_old = 0;
    uint16_t metadatamap_old = 0;
    uint16_t datamap = 0;
    uint16_t metadatamap = 0;
    uint16_t tmp_map = 0;
    uint16_t mask = 0;
    uint32_t event = 0;
    uint64_t val = 0;
    afr_private_t *priv = NULL;

    priv = this->private;
    txn_type = local->transaction.type;

    if (txn_type == AFR_DATA_TRANSACTION)
        val = local->inode_ctx->write_subvol;
    else
        val = local->inode_ctx->read_subvol;

    metadatamap_old = metadatamap = (val & 0x000000000000ffff);
    datamap_old = datamap = (val & 0x00000000ffff0000) >> 16;
    event = (val & 0xffffffff00000000) >> 32;

    if (txn_type == AFR_DATA_TRANSACTION)
        tmp_map = datamap;
    else if (txn_type == AFR_METADATA_TRANSACTION)
        tmp_map = metadatamap;

    count = gf_bits_count(tmp_map);

    for (i = 0; i < priv->child_count; i++) {
        if (!local->transaction.failed_subvols[i])
            continue;

        mask = 1 << i;
        if (txn_type == AFR_METADATA_TRANSACTION)
            metadatamap &= ~mask;
        else if (txn_type == AFR_DATA_TRANSACTION)
            datamap &= ~mask;
    }

    switch (txn_type) {
        case AFR_METADATA_TRANSACTION:
            if ((metadatamap_old != 0) && (metadatamap == 0) && (count == 1)) {
                index = gf_bits_index(tmp_map);
                local->transaction.in_flight_sb_errno = local->replies[index]
                                                            .op_errno;
                local->transaction.in_flight_sb = _gf_true;
                metadatamap |= (1 << index);
            }
            if (metadatamap_old != metadatamap) {
                __afr_inode_need_refresh_set(inode, this);
            }
            break;

        case AFR_DATA_TRANSACTION:
            if ((datamap_old != 0) && (datamap == 0) && (count == 1)) {
                index = gf_bits_index(tmp_map);
                local->transaction.in_flight_sb_errno = local->replies[index]
                                                            .op_errno;
                local->transaction.in_flight_sb = _gf_true;
                datamap |= (1 << index);
            }
            if (datamap_old != datamap)
                __afr_inode_need_refresh_set(inode, this);
            break;

        default:
            break;
    }

    val = ((uint64_t)metadatamap) | (((uint64_t)datamap) << 16) |
          (((uint64_t)event) << 32);

    if (txn_type == AFR_DATA_TRANSACTION)
        local->inode_ctx->write_subvol = val;
    local->inode_ctx->read_subvol = val;

    return 0;
}

gf_boolean_t
afr_is_symmetric_error(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int op_errno = 0;
    int i_errno = 0;
    gf_boolean_t matching_errors = _gf_true;
    int i = 0;

    priv = this->private;
    local = frame->local;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;
        if (local->replies[i].op_ret != -1) {
            /* Operation succeeded on at least one subvol,
               so it is not a failed-everywhere situation.
            */
            matching_errors = _gf_false;
            break;
        }
        i_errno = local->replies[i].op_errno;

        if (i_errno == ENOTCONN) {
            /* ENOTCONN is not a symmetric error. We do not
               know if the operation was performed on the
               backend or not.
            */
            matching_errors = _gf_false;
            break;
        }

        if (!op_errno) {
            op_errno = i_errno;
        } else if (op_errno != i_errno) {
            /* Mismatching op_errno's */
            matching_errors = _gf_false;
            break;
        }
    }

    return matching_errors;
}

int
afr_set_in_flight_sb_status(xlator_t *this, call_frame_t *frame, inode_t *inode)
{
    int ret = -1;
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    /* If this transaction saw no failures, then exit. */
    if (AFR_COUNT(local->transaction.failed_subvols, priv->child_count) == 0)
        return 0;

    if (afr_is_symmetric_error(frame, this))
        return 0;

    LOCK(&inode->lock);
    {
        ret = __afr_set_in_flight_sb_status(this, local, inode);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__afr_inode_read_subvol_get_small(inode_t *inode, xlator_t *this,
                                  unsigned char *data, unsigned char *metadata,
                                  int *event_p)
{
    afr_private_t *priv = NULL;
    int ret = -1;
    uint16_t datamap = 0;
    uint16_t metadatamap = 0;
    uint32_t event = 0;
    uint64_t val = 0;
    int i = 0;
    afr_inode_ctx_t *ctx = NULL;

    priv = this->private;

    ret = __afr_inode_ctx_get(this, inode, &ctx);
    if (ret < 0)
        return ret;

    val = ctx->read_subvol;

    metadatamap = (val & 0x000000000000ffff);
    datamap = (val & 0x00000000ffff0000) >> 16;
    event = (val & 0xffffffff00000000) >> 32;

    for (i = 0; i < priv->child_count; i++) {
        if (metadata)
            metadata[i] = (metadatamap >> i) & 1;
        if (data)
            data[i] = (datamap >> i) & 1;
    }

    if (event_p)
        *event_p = event;
    return ret;
}

int
__afr_inode_read_subvol_set_small(inode_t *inode, xlator_t *this,
                                  unsigned char *data, unsigned char *metadata,
                                  int event)
{
    afr_private_t *priv = NULL;
    uint16_t datamap = 0;
    uint16_t metadatamap = 0;
    uint64_t val = 0;
    int i = 0;
    int ret = -1;
    afr_inode_ctx_t *ctx = NULL;

    priv = this->private;

    ret = __afr_inode_ctx_get(this, inode, &ctx);
    if (ret)
        goto out;

    for (i = 0; i < priv->child_count; i++) {
        if (data[i])
            datamap |= (1 << i);
        if (metadata[i])
            metadatamap |= (1 << i);
    }

    val = ((uint64_t)metadatamap) | (((uint64_t)datamap) << 16) |
          (((uint64_t)event) << 32);

    ctx->read_subvol = val;

    ret = 0;
out:
    return ret;
}

int
__afr_inode_read_subvol_get(inode_t *inode, xlator_t *this, unsigned char *data,
                            unsigned char *metadata, int *event_p)
{
    afr_private_t *priv = NULL;
    int ret = -1;

    priv = this->private;

    if (priv->child_count <= 16)
        ret = __afr_inode_read_subvol_get_small(inode, this, data, metadata,
                                                event_p);
    else
        /* TBD: allocate structure with array and read from it */
        ret = -1;

    return ret;
}

int
__afr_inode_split_brain_choice_get(inode_t *inode, xlator_t *this,
                                   int *spb_choice)
{
    afr_inode_ctx_t *ctx = NULL;
    int ret = -1;

    ret = __afr_inode_ctx_get(this, inode, &ctx);
    if (ret < 0)
        return ret;

    *spb_choice = ctx->spb_choice;
    return 0;
}

int
__afr_inode_read_subvol_set(inode_t *inode, xlator_t *this, unsigned char *data,
                            unsigned char *metadata, int event)
{
    afr_private_t *priv = NULL;
    int ret = -1;

    priv = this->private;

    if (priv->child_count <= 16)
        ret = __afr_inode_read_subvol_set_small(inode, this, data, metadata,
                                                event);
    else
        ret = -1;

    return ret;
}

int
__afr_inode_split_brain_choice_set(inode_t *inode, xlator_t *this,
                                   int spb_choice)
{
    afr_inode_ctx_t *ctx = NULL;
    int ret = -1;

    ret = __afr_inode_ctx_get(this, inode, &ctx);
    if (ret)
        goto out;

    ctx->spb_choice = spb_choice;

    ret = 0;
out:
    return ret;
}

int
afr_inode_read_subvol_get(inode_t *inode, xlator_t *this, unsigned char *data,
                          unsigned char *metadata, int *event_p)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_read_subvol_get(inode, this, data, metadata, event_p);
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

int
afr_inode_get_readable(call_frame_t *frame, inode_t *inode, xlator_t *this,
                       unsigned char *readable, int *event_p, int type)
{
    afr_private_t *priv = this->private;
    afr_local_t *local = frame->local;
    unsigned char *data = alloca0(priv->child_count);
    unsigned char *metadata = alloca0(priv->child_count);
    int data_count = 0;
    int metadata_count = 0;
    int event_generation = 0;
    int ret = 0;

    ret = afr_inode_read_subvol_get(inode, this, data, metadata,
                                    &event_generation);
    if (ret == -1)
        return -EIO;

    data_count = AFR_COUNT(data, priv->child_count);
    metadata_count = AFR_COUNT(metadata, priv->child_count);

    if (inode->ia_type == IA_IFDIR) {
        /* For directories, allow even if it is in data split-brain. */
        if (type == AFR_METADATA_TRANSACTION || local->op == GF_FOP_STAT ||
            local->op == GF_FOP_FSTAT) {
            if (!metadata_count)
                return -EIO;
        }
    } else {
        /* For files, abort in case of data/metadata split-brain. */
        if (!data_count || !metadata_count) {
            return -EIO;
        }
    }

    if (type == AFR_METADATA_TRANSACTION && readable)
        memcpy(readable, metadata, priv->child_count * sizeof *metadata);
    if (type == AFR_DATA_TRANSACTION && readable) {
        if (!data_count)
            memcpy(readable, local->child_up,
                   priv->child_count * sizeof *readable);
        else
            memcpy(readable, data, priv->child_count * sizeof *data);
    }
    if (event_p)
        *event_p = event_generation;
    return 0;
}

static int
afr_inode_split_brain_choice_get(inode_t *inode, xlator_t *this,
                                 int *spb_choice)
{
    int ret = -1;
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_split_brain_choice_get(inode, this, spb_choice);
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

/*
 * frame is used to get the favourite policy. Since
 * afr_inode_split_brain_choice_get was called with afr_open, it is possible to
 * have a frame with out local->replies. So in that case, frame is passed as
 * null, hence this function will handle the frame NULL case.
 */
int
afr_split_brain_read_subvol_get(inode_t *inode, xlator_t *this,
                                call_frame_t *frame, int *spb_subvol)
{
    int ret = -1;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("afr", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);
    GF_VALIDATE_OR_GOTO(this->name, spb_subvol, out);

    priv = this->private;

    ret = afr_inode_split_brain_choice_get(inode, this, spb_subvol);
    if (*spb_subvol < 0 && priv->fav_child_policy && frame && frame->local) {
        local = frame->local;
        *spb_subvol = afr_sh_get_fav_by_policy(this, local->replies, inode,
                                               NULL);
        if (*spb_subvol >= 0) {
            ret = 0;
        }
    }

out:
    return ret;
}
int
afr_inode_read_subvol_set(inode_t *inode, xlator_t *this, unsigned char *data,
                          unsigned char *metadata, int event)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_read_subvol_set(inode, this, data, metadata, event);
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

int
afr_inode_split_brain_choice_set(inode_t *inode, xlator_t *this, int spb_choice)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_split_brain_choice_set(inode, this, spb_choice);
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

/* The caller of this should perform afr_inode_refresh, if this function
 * returns _gf_true
 */
gf_boolean_t
afr_is_inode_refresh_reqd(inode_t *inode, xlator_t *this, int event_gen1,
                          int event_gen2)
{
    gf_boolean_t need_refresh = _gf_false;
    afr_inode_ctx_t *ctx = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_ctx_get(this, inode, &ctx);
        if (ret)
            goto unlock;

        need_refresh = ctx->need_refresh;
        /* Hoping that the caller will do inode_refresh followed by
         * this, hence setting the need_refresh to false */
        ctx->need_refresh = _gf_false;
    }
unlock:
    UNLOCK(&inode->lock);

    if (event_gen1 != event_gen2)
        need_refresh = _gf_true;
out:
    return need_refresh;
}

int
__afr_inode_need_refresh_set(inode_t *inode, xlator_t *this)
{
    int ret = -1;
    afr_inode_ctx_t *ctx = NULL;

    ret = __afr_inode_ctx_get(this, inode, &ctx);
    if (ret == 0) {
        ctx->need_refresh = _gf_true;
    }

    return ret;
}

int
afr_inode_need_refresh_set(inode_t *inode, xlator_t *this)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_need_refresh_set(inode, this);
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

int
afr_spb_choice_timeout_cancel(xlator_t *this, inode_t *inode)
{
    afr_inode_ctx_t *ctx = NULL;
    int ret = -1;

    if (!inode)
        return ret;

    LOCK(&inode->lock);
    {
        ret = __afr_inode_ctx_get(this, inode, &ctx);
        if (ret < 0 || !ctx) {
            UNLOCK(&inode->lock);
            gf_msg(this->name, GF_LOG_WARNING, 0,
                   AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                   "Failed to cancel split-brain choice timer.");
            goto out;
        }
        ctx->spb_choice = -1;
        if (ctx->timer) {
            gf_timer_call_cancel(this->ctx, ctx->timer);
            ctx->timer = NULL;
        }
        ret = 0;
    }
    UNLOCK(&inode->lock);
out:
    return ret;
}

void
afr_set_split_brain_choice_cbk(void *data)
{
    inode_t *inode = data;
    xlator_t *this = THIS;

    afr_spb_choice_timeout_cancel(this, inode);
    inode_invalidate(inode);
    inode_unref(inode);
    return;
}

int
afr_set_split_brain_choice(int ret, call_frame_t *frame, void *opaque)
{
    int op_errno = ENOMEM;
    afr_private_t *priv = NULL;
    afr_inode_ctx_t *ctx = NULL;
    inode_t *inode = NULL;
    loc_t *loc = NULL;
    xlator_t *this = NULL;
    afr_spbc_timeout_t *data = opaque;
    struct timespec delta = {
        0,
    };
    gf_boolean_t timer_set = _gf_false;
    gf_boolean_t timer_cancelled = _gf_false;
    gf_boolean_t timer_reset = _gf_false;
    int old_spb_choice = -1;

    frame = data->frame;
    loc = data->loc;
    this = frame->this;
    priv = this->private;

    if (ret) {
        op_errno = -ret;
        ret = -1;
        goto out;
    }

    delta.tv_sec = priv->spb_choice_timeout;
    delta.tv_nsec = 0;

    if (!loc->inode) {
        ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    if (!(data->d_spb || data->m_spb)) {
        gf_msg(this->name, GF_LOG_WARNING, 0, AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
               "Cannot set "
               "replica.split-brain-choice on %s. File is"
               " not in data/metadata split-brain.",
               uuid_utoa(loc->gfid));
        ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    /*
     * we're ref'ing the inode before LOCK like it is done elsewhere in the
     * code. If we ref after LOCK, coverity complains of possible deadlocks.
     */
    inode = inode_ref(loc->inode);

    LOCK(&inode->lock);
    {
        ret = __afr_inode_ctx_get(this, inode, &ctx);
        if (ret) {
            UNLOCK(&inode->lock);
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                   "Failed to get inode_ctx for %s", loc->name);
            goto post_unlock;
        }

        old_spb_choice = ctx->spb_choice;
        ctx->spb_choice = data->spb_child_index;

        /* Possible changes in spb-choice :
         *         valid to -1    : cancel timer and unref
         *         valid to valid : cancel timer and inject new one
         *         -1    to -1    : unref and do not do anything
         *         -1 to valid    : inject timer
         */

        /* ctx->timer is NULL iff previous value of
         * ctx->spb_choice is -1
         */
        if (ctx->timer) {
            if (ctx->spb_choice == -1) {
                if (!gf_timer_call_cancel(this->ctx, ctx->timer)) {
                    ctx->timer = NULL;
                    timer_cancelled = _gf_true;
                }
                /* If timer cancel failed here it means that the
                 *  previous cbk will be executed which will set
                 *  spb_choice to -1. So we can consider the
                 *  'valid to -1' case to be a success
                 *  (i.e. ret = 0) and goto unlock.
                 */
                goto unlock;
            }
            goto reset_timer;
        } else {
            if (ctx->spb_choice == -1)
                goto unlock;
            goto set_timer;
        }

    reset_timer:
        ret = gf_timer_call_cancel(this->ctx, ctx->timer);
        if (ret != 0) {
            /* We need to bail out now instead of launching a new
             * timer. Otherwise the cbk of the previous timer event
             * will cancel the new ctx->timer.
             */
            ctx->spb_choice = old_spb_choice;
            ret = -1;
            op_errno = EAGAIN;
            goto unlock;
        }
        ctx->timer = NULL;
        timer_reset = _gf_true;

    set_timer:
        ctx->timer = gf_timer_call_after(this->ctx, delta,
                                         afr_set_split_brain_choice_cbk, inode);
        if (!ctx->timer) {
            ctx->spb_choice = old_spb_choice;
            ret = -1;
            op_errno = ENOMEM;
        }
        if (!timer_reset && ctx->timer)
            timer_set = _gf_true;
        if (timer_reset && !ctx->timer)
            timer_cancelled = _gf_true;
    }
unlock:
    UNLOCK(&inode->lock);
post_unlock:
    if (!timer_set)
        inode_unref(inode);
    if (timer_cancelled)
        inode_unref(inode);
    /*
     * We need to invalidate the inode to prevent the kernel from serving
     * reads from an older cached value despite a change in spb_choice to
     * a new value.
     */
    inode_invalidate(inode);
out:
    GF_FREE(data);
    AFR_STACK_UNWIND(setxattr, frame, ret, op_errno, NULL);
    return 0;
}

int
afr_accused_fill(xlator_t *this, dict_t *xdata, unsigned char *accused,
                 afr_transaction_type type)
{
    afr_private_t *priv = NULL;
    int i = 0;
    int idx = afr_index_for_transaction_type(type);
    void *pending_raw = NULL;
    int pending[3];
    int ret = 0;

    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        ret = dict_get_ptr(xdata, priv->pending_key[i], &pending_raw);
        if (ret) /* no pending flags */
            continue;
        memcpy(pending, pending_raw, sizeof(pending));

        if (ntoh32(pending[idx]))
            accused[i] = 1;
    }

    return 0;
}

int
afr_accuse_smallfiles(xlator_t *this, struct afr_reply *replies,
                      unsigned char *data_accused)
{
    int i = 0;
    afr_private_t *priv = NULL;
    uint64_t maxsize = 0;

    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (replies[i].valid && replies[i].xdata &&
            dict_get_sizen(replies[i].xdata, GLUSTERFS_BAD_INODE))
            continue;
        if (data_accused[i])
            continue;
        if (replies[i].poststat.ia_size > maxsize)
            maxsize = replies[i].poststat.ia_size;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (data_accused[i])
            continue;
        if (AFR_IS_ARBITER_BRICK(priv, i))
            continue;
        if (replies[i].poststat.ia_size < maxsize)
            data_accused[i] = 1;
    }

    return 0;
}

int
afr_readables_fill(call_frame_t *frame, xlator_t *this, inode_t *inode,
                   unsigned char *data_accused, unsigned char *metadata_accused,
                   unsigned char *data_readable,
                   unsigned char *metadata_readable, struct afr_reply *replies)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    dict_t *xdata = NULL;
    int i = 0;
    int ret = 0;
    ia_type_t ia_type = IA_INVAL;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        data_readable[i] = 1;
        metadata_readable[i] = 1;
    }
    if (AFR_IS_ARBITER_BRICK(priv, ARBITER_BRICK_INDEX)) {
        data_readable[ARBITER_BRICK_INDEX] = 0;
        metadata_readable[ARBITER_BRICK_INDEX] = 0;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (replies) { /* Lookup */
            if (!replies[i].valid || replies[i].op_ret == -1 ||
                (replies[i].xdata &&
                 dict_get_sizen(replies[i].xdata, GLUSTERFS_BAD_INODE))) {
                data_readable[i] = 0;
                metadata_readable[i] = 0;
                continue;
            }

            xdata = replies[i].xdata;
            ia_type = replies[i].poststat.ia_type;
        } else { /* pre-op xattrop */
            xdata = local->transaction.changelog_xdata[i];
            ia_type = inode->ia_type;
        }

        if (!xdata)
            continue; /* mkdir_cbk sends NULL xdata_rsp. */
        afr_accused_fill(this, xdata, data_accused,
                         (ia_type == IA_IFDIR) ? AFR_ENTRY_TRANSACTION
                                               : AFR_DATA_TRANSACTION);

        afr_accused_fill(this, xdata, metadata_accused,
                         AFR_METADATA_TRANSACTION);
    }

    if (replies && ia_type != IA_INVAL && ia_type != IA_IFDIR &&
        /* We want to accuse small files only when we know for
         * sure that there is no IO happening. Otherwise, the
         * ia_sizes obtained in post-refresh replies may
         * mismatch due to a race between inode-refresh and
         * ongoing writes, causing spurious heal launches*/
        !afr_is_possibly_under_txn(AFR_DATA_TRANSACTION, local, this)) {
        afr_accuse_smallfiles(this, replies, data_accused);
    }

    for (i = 0; i < priv->child_count; i++) {
        if (data_accused[i]) {
            data_readable[i] = 0;
            ret = 1;
        }
        if (metadata_accused[i]) {
            metadata_readable[i] = 0;
            ret = 1;
        }
    }
    return ret;
}

int
afr_replies_interpret(call_frame_t *frame, xlator_t *this, inode_t *inode,
                      gf_boolean_t *start_heal)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;
    int event_generation = 0;
    int i = 0;
    unsigned char *data_accused = NULL;
    unsigned char *metadata_accused = NULL;
    unsigned char *data_readable = NULL;
    unsigned char *metadata_readable = NULL;
    int ret = 0;

    local = frame->local;
    priv = this->private;
    replies = local->replies;
    event_generation = local->event_generation;

    data_accused = alloca0(priv->child_count);
    data_readable = alloca0(priv->child_count);
    metadata_accused = alloca0(priv->child_count);
    metadata_readable = alloca0(priv->child_count);

    ret = afr_readables_fill(frame, this, inode, data_accused, metadata_accused,
                             data_readable, metadata_readable, replies);

    for (i = 0; i < priv->child_count; i++) {
        if (start_heal && priv->child_up[i] &&
            (data_accused[i] || metadata_accused[i])) {
            *start_heal = _gf_true;
            break;
        }
    }
    afr_inode_read_subvol_set(inode, this, data_readable, metadata_readable,
                              event_generation);
    return ret;
}

int
afr_refresh_selfheal_done(int ret, call_frame_t *heal, void *opaque)
{
    if (heal)
        AFR_STACK_DESTROY(heal);
    return 0;
}

int
afr_inode_refresh_err(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int err = 0;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (local->replies[i].valid && !local->replies[i].op_ret) {
            err = 0;
            goto ret;
        }
    }

    err = afr_final_errno(local, priv);
ret:
    return err;
}

gf_boolean_t
afr_selfheal_enabled(const xlator_t *this)
{
    const afr_private_t *priv = this->private;

    return priv->data_self_heal || priv->metadata_self_heal ||
           priv->entry_self_heal;
}

int
afr_txn_refresh_done(call_frame_t *frame, xlator_t *this, int err)
{
    call_frame_t *heal_frame = NULL;
    afr_local_t *heal_local = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    inode_t *inode = NULL;
    int event_generation = 0;
    int read_subvol = -1;
    int ret = 0;

    local = frame->local;
    inode = local->inode;
    priv = this->private;

    if (err)
        goto refresh_done;

    if (local->op == GF_FOP_LOOKUP)
        goto refresh_done;

    ret = afr_inode_get_readable(frame, inode, this, local->readable,
                                 &event_generation, local->transaction.type);

    if (ret == -EIO) {
        /* No readable subvolume even after refresh ==> splitbrain.*/
        if (!priv->fav_child_policy) {
            err = EIO;
            goto refresh_done;
        }
        read_subvol = afr_sh_get_fav_by_policy(this, local->replies, inode,
                                               NULL);
        if (read_subvol == -1) {
            err = EIO;
            goto refresh_done;
        }

        heal_frame = afr_frame_create(this, NULL);
        if (!heal_frame) {
            err = EIO;
            goto refresh_done;
        }
        heal_local = heal_frame->local;
        heal_local->xdata_req = dict_new();
        if (!heal_local->xdata_req) {
            err = EIO;
            AFR_STACK_DESTROY(heal_frame);
            goto refresh_done;
        }
        heal_local->heal_frame = frame;
        ret = synctask_new(this->ctx->env, afr_fav_child_reset_sink_xattrs,
                           afr_fav_child_reset_sink_xattrs_cbk, heal_frame,
                           heal_frame);
        return 0;
    }

refresh_done:
    afr_local_replies_wipe(local, this->private);
    local->refreshfn(frame, this, err);

    return 0;
}

int
afr_inode_refresh_done(call_frame_t *frame, xlator_t *this, int error)
{
    call_frame_t *heal_frame = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    gf_boolean_t start_heal = _gf_false;
    afr_local_t *heal_local = NULL;
    unsigned char *success_replies = NULL;
    int ret = 0;

    if (error != 0) {
        goto refresh_done;
    }

    local = frame->local;
    priv = this->private;
    success_replies = alloca0(priv->child_count);
    afr_fill_success_replies(local, priv, success_replies);

    if (priv->thin_arbiter_count && local->is_read_txn &&
        AFR_COUNT(success_replies, priv->child_count) != priv->child_count) {
        /* We need to query the good bricks and/or thin-arbiter.*/
        if (success_replies[0]) {
            local->read_txn_query_child = AFR_CHILD_ZERO;
        } else if (success_replies[1]) {
            local->read_txn_query_child = AFR_CHILD_ONE;
        }
        error = EINVAL;
        goto refresh_done;
    }

    if (!afr_has_quorum(success_replies, this, frame)) {
        error = afr_final_errno(frame->local, this->private);
        if (!error)
            error = afr_quorum_errno(priv);
        goto refresh_done;
    }

    ret = afr_replies_interpret(frame, this, local->refreshinode, &start_heal);

    if (ret && afr_selfheal_enabled(this) && start_heal) {
        heal_frame = afr_frame_create(this, NULL);
        if (!heal_frame)
            goto refresh_done;
        heal_local = heal_frame->local;
        heal_local->refreshinode = inode_ref(local->refreshinode);
        heal_local->heal_frame = heal_frame;
        if (!afr_throttled_selfheal(heal_frame, this)) {
            AFR_STACK_DESTROY(heal_frame);
            goto refresh_done;
        }
    }

refresh_done:
    afr_txn_refresh_done(frame, this, error);

    return 0;
}

void
afr_inode_refresh_subvol_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, struct iatt *buf,
                             dict_t *xdata, struct iatt *par)
{
    afr_local_t *local = NULL;
    int call_child = (long)cookie;
    int8_t need_heal = 1;
    int call_count = 0;
    int ret = 0;

    local = frame->local;
    local->replies[call_child].valid = 1;
    local->replies[call_child].op_ret = op_ret;
    local->replies[call_child].op_errno = op_errno;
    if (op_ret != -1) {
        local->replies[call_child].poststat = *buf;
        if (par)
            local->replies[call_child].postparent = *par;
        if (xdata)
            local->replies[call_child].xdata = dict_ref(xdata);
    }

    if (xdata) {
        ret = dict_get_int8(xdata, "link-count", &need_heal);
        if (ret) {
            gf_msg_debug(this->name, -ret, "Unable to get link count");
        }
    }

    local->replies[call_child].need_heal = need_heal;
    call_count = afr_frame_return(frame);
    if (call_count == 0) {
        afr_set_need_heal(this, local);
        ret = afr_inode_refresh_err(frame, this);
        if (ret) {
            gf_msg_debug(this->name, ret, "afr_inode_refresh_err failed");
        }
        afr_inode_refresh_done(frame, this, ret);
    }
}

int
afr_inode_refresh_subvol_with_lookup_cbk(call_frame_t *frame, void *cookie,
                                         xlator_t *this, int op_ret,
                                         int op_errno, inode_t *inode,
                                         struct iatt *buf, dict_t *xdata,
                                         struct iatt *par)
{
    afr_inode_refresh_subvol_cbk(frame, cookie, this, op_ret, op_errno, buf,
                                 xdata, par);
    return 0;
}

int
afr_inode_refresh_subvol_with_lookup(call_frame_t *frame, xlator_t *this, int i,
                                     inode_t *inode, uuid_t gfid, dict_t *xdata)
{
    loc_t loc = {
        0,
    };
    afr_private_t *priv = NULL;

    priv = this->private;

    loc.inode = inode;
    if (gf_uuid_is_null(inode->gfid) && gfid) {
        /* To handle setattr/setxattr on yet to be linked inode from
         * dht */
        gf_uuid_copy(loc.gfid, gfid);
    } else {
        gf_uuid_copy(loc.gfid, inode->gfid);
    }

    STACK_WIND_COOKIE(frame, afr_inode_refresh_subvol_with_lookup_cbk,
                      (void *)(long)i, priv->children[i],
                      priv->children[i]->fops->lookup, &loc, xdata);
    return 0;
}

int
afr_inode_refresh_subvol_with_fstat_cbk(call_frame_t *frame, void *cookie,
                                        xlator_t *this, int32_t op_ret,
                                        int32_t op_errno, struct iatt *buf,
                                        dict_t *xdata)
{
    afr_inode_refresh_subvol_cbk(frame, cookie, this, op_ret, op_errno, buf,
                                 xdata, NULL);
    return 0;
}

int
afr_inode_refresh_subvol_with_fstat(call_frame_t *frame, xlator_t *this, int i,
                                    dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    STACK_WIND_COOKIE(frame, afr_inode_refresh_subvol_with_fstat_cbk,
                      (void *)(long)i, priv->children[i],
                      priv->children[i]->fops->fstat, local->fd, xdata);
    return 0;
}

int
afr_inode_refresh_do(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = 0;
    int i = 0;
    int ret = 0;
    dict_t *xdata = NULL;
    afr_fd_ctx_t *fd_ctx = NULL;
    unsigned char *wind_subvols = NULL;

    priv = this->private;
    local = frame->local;
    wind_subvols = alloca0(priv->child_count);

    afr_local_replies_wipe(local, priv);

    if (local->fd) {
        fd_ctx = afr_fd_ctx_get(local->fd, this);
        if (!fd_ctx) {
            afr_inode_refresh_done(frame, this, EINVAL);
            return 0;
        }
    }

    xdata = dict_new();
    if (!xdata) {
        afr_inode_refresh_done(frame, this, ENOMEM);
        return 0;
    }

    ret = afr_xattr_req_prepare(this, xdata);
    if (ret != 0) {
        dict_unref(xdata);
        afr_inode_refresh_done(frame, this, -ret);
        return 0;
    }

    ret = dict_set_sizen_str_sizen(xdata, "link-count", GF_XATTROP_INDEX_COUNT);
    if (ret) {
        gf_msg_debug(this->name, -ret, "Unable to set link-count in dict ");
    }

    ret = dict_set_str_sizen(xdata, GLUSTERFS_INODELK_DOM_COUNT, this->name);
    if (ret) {
        gf_msg_debug(this->name, -ret,
                     "Unable to set inodelk-dom-count in dict ");
    }

    if (local->fd) {
        for (i = 0; i < priv->child_count; i++) {
            if (local->child_up[i] && fd_ctx->opened_on[i] == AFR_FD_OPENED)
                wind_subvols[i] = 1;
        }
    } else {
        memcpy(wind_subvols, local->child_up,
               sizeof(*local->child_up) * priv->child_count);
    }

    local->call_count = AFR_COUNT(wind_subvols, priv->child_count);

    call_count = local->call_count;
    if (!call_count) {
        dict_unref(xdata);
        if (local->fd && AFR_COUNT(local->child_up, priv->child_count))
            afr_inode_refresh_done(frame, this, EBADFD);
        else
            afr_inode_refresh_done(frame, this, ENOTCONN);
        return 0;
    }
    for (i = 0; i < priv->child_count; i++) {
        if (!wind_subvols[i])
            continue;

        if (local->fd)
            afr_inode_refresh_subvol_with_fstat(frame, this, i, xdata);
        else
            afr_inode_refresh_subvol_with_lookup(
                frame, this, i, local->refreshinode, local->refreshgfid, xdata);

        if (!--call_count)
            break;
    }

    dict_unref(xdata);

    return 0;
}

int
afr_inode_refresh(call_frame_t *frame, xlator_t *this, inode_t *inode,
                  uuid_t gfid, afr_inode_refresh_cbk_t refreshfn)
{
    afr_local_t *local = NULL;

    local = frame->local;

    local->refreshfn = refreshfn;

    if (local->refreshinode) {
        inode_unref(local->refreshinode);
        local->refreshinode = NULL;
    }

    local->refreshinode = inode_ref(inode);

    if (gfid)
        gf_uuid_copy(local->refreshgfid, gfid);
    else
        gf_uuid_clear(local->refreshgfid);

    afr_inode_refresh_do(frame, this);

    return 0;
}

int
afr_xattr_req_prepare(xlator_t *this, dict_t *xattr_req)
{
    int i = 0;
    afr_private_t *priv = NULL;
    int ret = 0;

    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        ret = dict_set_uint64(xattr_req, priv->pending_key[i],
                              AFR_NUM_CHANGE_LOGS * sizeof(int));
        if (ret < 0)
            gf_msg(this->name, GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Unable to set dict value for %s", priv->pending_key[i]);
        /* 3 = data+metadata+entry */
    }
    ret = dict_set_uint64(xattr_req, AFR_DIRTY,
                          AFR_NUM_CHANGE_LOGS * sizeof(int));
    if (ret) {
        gf_msg_debug(this->name, -ret,
                     "failed to set dirty "
                     "query flag");
    }

    ret = dict_set_int32_sizen(xattr_req, "list-xattr", 1);
    if (ret) {
        gf_msg_debug(this->name, -ret, "Unable to set list-xattr in dict ");
    }

    return ret;
}

int
afr_lookup_xattr_req_prepare(afr_local_t *local, xlator_t *this,
                             dict_t *xattr_req, loc_t *loc)
{
    int ret = -ENOMEM;

    if (!local->xattr_req)
        local->xattr_req = dict_new();

    if (!local->xattr_req)
        goto out;

    if (xattr_req && (xattr_req != local->xattr_req))
        dict_copy(xattr_req, local->xattr_req);

    ret = afr_xattr_req_prepare(this, local->xattr_req);

    ret = dict_set_uint64(local->xattr_req, GLUSTERFS_INODELK_COUNT, 0);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
               "%s: Unable to set dict value for %s", loc->path,
               GLUSTERFS_INODELK_COUNT);
    }
    ret = dict_set_uint64(local->xattr_req, GLUSTERFS_ENTRYLK_COUNT, 0);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
               "%s: Unable to set dict value for %s", loc->path,
               GLUSTERFS_ENTRYLK_COUNT);
    }

    ret = dict_set_uint32(local->xattr_req, GLUSTERFS_PARENT_ENTRYLK, 0);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
               "%s: Unable to set dict value for %s", loc->path,
               GLUSTERFS_PARENT_ENTRYLK);
    }

    ret = dict_set_sizen_str_sizen(local->xattr_req, "link-count",
                                   GF_XATTROP_INDEX_COUNT);
    if (ret) {
        gf_msg_debug(this->name, -ret, "Unable to set link-count in dict ");
    }

    ret = 0;
out:
    return ret;
}

int
afr_least_pending_reads_child(afr_private_t *priv, unsigned char *readable)
{
    int i = 0;
    int child = -1;
    int64_t read_iter = -1;
    int64_t pending_read = -1;

    for (i = 0; i < priv->child_count; i++) {
        if (AFR_IS_ARBITER_BRICK(priv, i) || !readable[i])
            continue;
        read_iter = GF_ATOMIC_GET(priv->pending_reads[i]);
        if (child == -1 || read_iter < pending_read) {
            pending_read = read_iter;
            child = i;
        }
    }

    return child;
}

static int32_t
afr_least_latency_child(afr_private_t *priv, unsigned char *readable)
{
    int32_t i = 0;
    int child = -1;

    for (i = 0; i < priv->child_count; i++) {
        if (AFR_IS_ARBITER_BRICK(priv, i) || !readable[i] ||
            priv->child_latency[i] < 0)
            continue;

        if (child == -1 ||
            priv->child_latency[i] < priv->child_latency[child]) {
            child = i;
        }
    }
    return child;
}

static int32_t
afr_least_latency_times_pending_reads_child(afr_private_t *priv,
                                            unsigned char *readable)
{
    int32_t i = 0;
    int child = -1;
    int64_t pending_read = 0;
    int64_t latency = -1;
    int64_t least_latency = -1;

    for (i = 0; i < priv->child_count; i++) {
        if (AFR_IS_ARBITER_BRICK(priv, i) || !readable[i] ||
            priv->child_latency[i] < 0)
            continue;

        pending_read = GF_ATOMIC_GET(priv->pending_reads[i]);
        latency = (pending_read + 1) * priv->child_latency[i];

        if (child == -1 || latency < least_latency) {
            least_latency = latency;
            child = i;
        }
    }
    return child;
}

int
afr_hash_child(afr_read_subvol_args_t *args, afr_private_t *priv,
               unsigned char *readable)
{
    uuid_t gfid_copy = {
        0,
    };
    pid_t pid;
    int child = -1;

    switch (priv->hash_mode) {
        case AFR_READ_POLICY_FIRST_UP:
            break;
        case AFR_READ_POLICY_GFID_HASH:
            gf_uuid_copy(gfid_copy, args->gfid);
            child = SuperFastHash((char *)gfid_copy, sizeof(gfid_copy)) %
                    priv->child_count;
            break;
        case AFR_READ_POLICY_GFID_PID_HASH:
            if (args->ia_type != IA_IFDIR) {
                /*
                 * Why getpid?  Because it's one of the cheapest calls
                 * available - faster than gethostname etc. - and
                 * returns a constant-length value that's sure to be
                 * shorter than a UUID. It's still very unlikely to be
                 * the same across clients, so it still provides good
                 * mixing.  We're not trying for perfection here. All we
                 * need is a low probability that multiple clients
                 * won't converge on the same subvolume.
                 */
                gf_uuid_copy(gfid_copy, args->gfid);
                pid = getpid();
                *(pid_t *)gfid_copy ^= pid;
            }
            child = SuperFastHash((char *)gfid_copy, sizeof(gfid_copy)) %
                    priv->child_count;
            break;
        case AFR_READ_POLICY_LESS_LOAD:
            child = afr_least_pending_reads_child(priv, readable);
            break;
        case AFR_READ_POLICY_LEAST_LATENCY:
            child = afr_least_latency_child(priv, readable);
            break;
        case AFR_READ_POLICY_LOAD_LATENCY_HYBRID:
            child = afr_least_latency_times_pending_reads_child(priv, readable);
            break;
    }

    return child;
}

int
afr_read_subvol_select_by_policy(inode_t *inode, xlator_t *this,
                                 unsigned char *readable,
                                 afr_read_subvol_args_t *args)
{
    int i = 0;
    int read_subvol = -1;
    afr_private_t *priv = NULL;
    afr_read_subvol_args_t local_args = {
        0,
    };

    priv = this->private;

    /* first preference - explicitly specified or local subvolume */
    if (priv->read_child >= 0 && readable[priv->read_child])
        return priv->read_child;

    if (inode_is_linked(inode)) {
        gf_uuid_copy(local_args.gfid, inode->gfid);
        local_args.ia_type = inode->ia_type;
    } else if (args) {
        local_args = *args;
    }

    /* second preference - use hashed mode */
    read_subvol = afr_hash_child(&local_args, priv, readable);
    if (read_subvol >= 0 && readable[read_subvol])
        return read_subvol;

    for (i = 0; i < priv->child_count; i++) {
        if (readable[i])
            return i;
    }

    /* no readable subvolumes, either split brain or all subvols down */

    return -1;
}

int
afr_inode_read_subvol_type_get(inode_t *inode, xlator_t *this,
                               unsigned char *readable, int *event_p, int type)
{
    int ret = -1;

    if (type == AFR_METADATA_TRANSACTION)
        ret = afr_inode_read_subvol_get(inode, this, 0, readable, event_p);
    else
        ret = afr_inode_read_subvol_get(inode, this, readable, 0, event_p);
    return ret;
}

void
afr_readables_intersect_get(inode_t *inode, xlator_t *this, int *event,
                            unsigned char *intersection)
{
    afr_private_t *priv = NULL;
    unsigned char *data_readable = NULL;
    unsigned char *metadata_readable = NULL;
    unsigned char *intersect = NULL;

    priv = this->private;
    data_readable = alloca0(priv->child_count);
    metadata_readable = alloca0(priv->child_count);
    intersect = alloca0(priv->child_count);

    afr_inode_read_subvol_get(inode, this, data_readable, metadata_readable,
                              event);

    AFR_INTERSECT(intersect, data_readable, metadata_readable,
                  priv->child_count);
    if (intersection)
        memcpy(intersection, intersect,
               sizeof(*intersection) * priv->child_count);
}

int
afr_read_subvol_get(inode_t *inode, xlator_t *this, int *subvol_p,
                    unsigned char *readables, int *event_p,
                    afr_transaction_type type, afr_read_subvol_args_t *args)
{
    afr_private_t *priv = NULL;
    unsigned char *readable = NULL;
    unsigned char *intersection = NULL;
    int subvol = -1;
    int event = 0;

    priv = this->private;

    readable = alloca0(priv->child_count);
    intersection = alloca0(priv->child_count);

    afr_inode_read_subvol_type_get(inode, this, readable, &event, type);

    afr_readables_intersect_get(inode, this, &event, intersection);

    if (AFR_COUNT(intersection, priv->child_count) > 0)
        subvol = afr_read_subvol_select_by_policy(inode, this, intersection,
                                                  args);
    else
        subvol = afr_read_subvol_select_by_policy(inode, this, readable, args);
    if (subvol_p)
        *subvol_p = subvol;
    if (event_p)
        *event_p = event;
    if (readables)
        memcpy(readables, readable, sizeof(*readables) * priv->child_count);
    return subvol;
}

void
afr_local_transaction_cleanup(afr_local_t *local, xlator_t *this)
{
    afr_private_t *priv = NULL;
    int i = 0;

    priv = this->private;

    afr_matrix_cleanup(local->pending, priv->child_count);

    GF_FREE(local->internal_lock.lower_locked_nodes);

    afr_lockees_cleanup(&local->internal_lock);

    GF_FREE(local->transaction.pre_op);

    GF_FREE(local->transaction.pre_op_sources);
    if (local->transaction.changelog_xdata) {
        for (i = 0; i < priv->child_count; i++) {
            if (!local->transaction.changelog_xdata[i])
                continue;
            dict_unref(local->transaction.changelog_xdata[i]);
        }
        GF_FREE(local->transaction.changelog_xdata);
    }

    GF_FREE(local->transaction.failed_subvols);

    GF_FREE(local->transaction.basename);
    GF_FREE(local->transaction.new_basename);

    loc_wipe(&local->transaction.parent_loc);
    loc_wipe(&local->transaction.new_parent_loc);
}

void
afr_reply_wipe(struct afr_reply *reply)
{
    if (reply->xdata) {
        dict_unref(reply->xdata);
        reply->xdata = NULL;
    }

    if (reply->xattr) {
        dict_unref(reply->xattr);
        reply->xattr = NULL;
    }
}

void
afr_replies_wipe(struct afr_reply *replies, int count)
{
    int i = 0;

    for (i = 0; i < count; i++) {
        afr_reply_wipe(&replies[i]);
    }
}

void
afr_local_replies_wipe(afr_local_t *local, afr_private_t *priv)
{
    if (!local->replies)
        return;

    afr_replies_wipe(local->replies, priv->child_count);

    memset(local->replies, 0, sizeof(*local->replies) * priv->child_count);
}

static gf_boolean_t
afr_fop_lock_is_unlock(call_frame_t *frame)
{
    afr_local_t *local = frame->local;
    switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
            if ((F_UNLCK == local->cont.inodelk.in_flock.l_type) &&
                (local->cont.inodelk.in_cmd == F_SETLKW ||
                 local->cont.inodelk.in_cmd == F_SETLK))
                return _gf_true;
            break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
            if (ENTRYLK_UNLOCK == local->cont.entrylk.in_cmd)
                return _gf_true;
            break;
        default:
            return _gf_false;
    }
    return _gf_false;
}

static gf_boolean_t
afr_lk_is_unlock(int32_t cmd, struct gf_flock *flock)
{
    switch (cmd) {
        case F_RESLK_UNLCK:
            return _gf_true;
            break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
            if (F_UNLCK == flock->l_type)
                return _gf_true;
            break;
        default:
            return _gf_false;
    }
    return _gf_false;
}

void
afr_handle_inconsistent_fop(call_frame_t *frame, int32_t *op_ret,
                            int32_t *op_errno)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    if (!frame || !frame->this || !frame->local || !frame->this->private)
        return;

    if (*op_ret < 0)
        return;

    /* Failing inodelk/entrylk/lk here is not a good idea because we
     * need to cleanup the locks on the other bricks if we choose to fail
     * the fop here. The brick may go down just after unwind happens as well
     * so anyways the fop will fail when the next fop is sent so leaving
     * it like this for now.*/
    local = frame->local;
    switch (local->op) {
        case GF_FOP_LOOKUP:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
        case GF_FOP_LK:
            return;
        default:
            break;
    }

    priv = frame->this->private;
    if (!priv->consistent_io)
        return;

    if (local->event_generation &&
        (local->event_generation != priv->event_generation))
        goto inconsistent;

    return;
inconsistent:
    *op_ret = -1;
    *op_errno = ENOTCONN;
}

void
afr_local_cleanup(afr_local_t *local, xlator_t *this)
{
    afr_private_t *priv = NULL;

    if (!local)
        return;

    syncbarrier_destroy(&local->barrier);

    afr_local_transaction_cleanup(local, this);

    priv = this->private;

    loc_wipe(&local->loc);
    loc_wipe(&local->newloc);

    if (local->fd)
        fd_unref(local->fd);

    if (local->xattr_req)
        dict_unref(local->xattr_req);

    if (local->xattr_rsp)
        dict_unref(local->xattr_rsp);

    if (local->dict)
        dict_unref(local->dict);

    afr_local_replies_wipe(local, priv);
    GF_FREE(local->replies);

    GF_FREE(local->child_up);

    GF_FREE(local->read_attempted);

    GF_FREE(local->readable);
    GF_FREE(local->readable2);

    if (local->inode)
        inode_unref(local->inode);

    if (local->parent)
        inode_unref(local->parent);

    if (local->parent2)
        inode_unref(local->parent2);

    if (local->refreshinode)
        inode_unref(local->refreshinode);

    { /* getxattr */
        GF_FREE(local->cont.getxattr.name);
    }

    { /* lk */
        GF_FREE(local->cont.lk.locked_nodes);
        GF_FREE(local->cont.lk.dom_locked_nodes);
        GF_FREE(local->cont.lk.dom_lock_op_ret);
        GF_FREE(local->cont.lk.dom_lock_op_errno);
    }

    { /* create */
        if (local->cont.create.fd)
            fd_unref(local->cont.create.fd);
        if (local->cont.create.params)
            dict_unref(local->cont.create.params);
    }

    { /* mknod */
        if (local->cont.mknod.params)
            dict_unref(local->cont.mknod.params);
    }

    { /* mkdir */
        if (local->cont.mkdir.params)
            dict_unref(local->cont.mkdir.params);
    }

    { /* symlink */
        if (local->cont.symlink.params)
            dict_unref(local->cont.symlink.params);
    }

    { /* writev */
        GF_FREE(local->cont.writev.vector);
        if (local->cont.writev.iobref)
            iobref_unref(local->cont.writev.iobref);
    }

    { /* setxattr */
        if (local->cont.setxattr.dict)
            dict_unref(local->cont.setxattr.dict);
    }

    { /* fsetxattr */
        if (local->cont.fsetxattr.dict)
            dict_unref(local->cont.fsetxattr.dict);
    }

    { /* removexattr */
        GF_FREE(local->cont.removexattr.name);
    }
    { /* xattrop */
        if (local->cont.xattrop.xattr)
            dict_unref(local->cont.xattrop.xattr);
    }
    { /* symlink */
        GF_FREE(local->cont.symlink.linkpath);
    }

    { /* opendir */
        GF_FREE(local->cont.opendir.checksum);
    }

    { /* open */
        if (local->cont.open.fd)
            fd_unref(local->cont.open.fd);
    }

    { /* readdirp */
        if (local->cont.readdir.dict)
            dict_unref(local->cont.readdir.dict);
    }

    { /* inodelk */
        GF_FREE(local->cont.inodelk.volume);
        if (local->cont.inodelk.xdata)
            dict_unref(local->cont.inodelk.xdata);
    }

    { /* entrylk */
        GF_FREE(local->cont.entrylk.volume);
        GF_FREE(local->cont.entrylk.basename);
        if (local->cont.entrylk.xdata)
            dict_unref(local->cont.entrylk.xdata);
    }

    if (local->xdata_req)
        dict_unref(local->xdata_req);

    if (local->xdata_rsp)
        dict_unref(local->xdata_rsp);
}

int
afr_frame_return(call_frame_t *frame)
{
    afr_local_t *local = NULL;
    int call_count = 0;

    local = frame->local;

    LOCK(&frame->lock);
    {
        call_count = --local->call_count;
    }
    UNLOCK(&frame->lock);

    return call_count;
}

static char *afr_ignore_xattrs[] = {GF_SELINUX_XATTR_KEY, QUOTA_SIZE_KEY, NULL};

gf_boolean_t
afr_is_xattr_ignorable(char *key)
{
    int i = 0;

    if (!strncmp(key, AFR_XATTR_PREFIX, SLEN(AFR_XATTR_PREFIX)))
        return _gf_true;
    for (i = 0; afr_ignore_xattrs[i]; i++) {
        if (!strcmp(key, afr_ignore_xattrs[i]))
            return _gf_true;
    }
    return _gf_false;
}

static gf_boolean_t
afr_xattr_match_needed(dict_t *this, char *key1, data_t *value1, void *data)
{
    /* Ignore all non-disk (i.e. virtual) xattrs right away. */
    if (!gf_is_valid_xattr_namespace(key1))
        return _gf_false;

    /* Ignore on-disk xattrs that AFR doesn't need to heal. */
    if (!afr_is_xattr_ignorable(key1))
        return _gf_true;

    return _gf_false;
}

gf_boolean_t
afr_xattrs_are_equal(dict_t *dict1, dict_t *dict2)
{
    return are_dicts_equal(dict1, dict2, afr_xattr_match_needed, NULL);
}

static int
afr_get_parent_read_subvol(xlator_t *this, inode_t *parent,
                           struct afr_reply *replies, unsigned char *readable)
{
    int i = 0;
    int par_read_subvol = -1;
    int par_read_subvol_iter = -1;
    afr_private_t *priv = NULL;

    priv = this->private;

    if (parent)
        par_read_subvol = afr_data_subvol_get(parent, this, NULL, NULL, NULL,
                                              NULL);

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid)
            continue;

        if (replies[i].op_ret < 0)
            continue;

        if (par_read_subvol_iter == -1) {
            par_read_subvol_iter = i;
            continue;
        }

        if ((par_read_subvol_iter != par_read_subvol) && readable[i])
            par_read_subvol_iter = i;

        if (i == par_read_subvol)
            par_read_subvol_iter = i;
    }
    /* At the end of the for-loop, the only reason why @par_read_subvol_iter
     * could be -1 is when this LOOKUP has failed on all sub-volumes.
     * So it is okay to send an arbitrary subvolume (0 in this case)
     * as parent read subvol.
     */
    if (par_read_subvol_iter == -1)
        par_read_subvol_iter = 0;

    return par_read_subvol_iter;
}

int
afr_read_subvol_decide(inode_t *inode, xlator_t *this,
                       afr_read_subvol_args_t *args, unsigned char *readable)
{
    int event = 0;
    afr_private_t *priv = NULL;
    unsigned char *intersection = NULL;

    priv = this->private;
    intersection = alloca0(priv->child_count);

    afr_readables_intersect_get(inode, this, &event, intersection);

    if (AFR_COUNT(intersection, priv->child_count) <= 0) {
        /* TODO: If we have one brick with valid data_readable and
         * another with metadata_readable, try to send an iatt with
         * valid bits from both.*/
        return -1;
    }

    memcpy(readable, intersection, sizeof(*readable) * priv->child_count);

    return afr_read_subvol_select_by_policy(inode, this, intersection, args);
}

static inline int
afr_first_up_child(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++)
        if (local->replies[i].valid && local->replies[i].op_ret == 0)
            return i;
    return -1;
}

static void
afr_attempt_readsubvol_set(call_frame_t *frame, xlator_t *this,
                           unsigned char *success_replies,
                           unsigned char *data_readable, int *read_subvol)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int spb_subvol = -1;
    int child_count = -1;

    if (*read_subvol != -1)
        return;

    priv = this->private;
    local = frame->local;
    child_count = priv->child_count;

    afr_split_brain_read_subvol_get(local->inode, this, frame, &spb_subvol);
    if ((spb_subvol >= 0) &&
        (AFR_COUNT(success_replies, child_count) == child_count)) {
        *read_subvol = spb_subvol;
    } else if (!priv->quorum_count ||
               frame->root->pid == GF_CLIENT_PID_GLFS_HEAL) {
        *read_subvol = afr_first_up_child(frame, this);
    } else if (priv->quorum_count &&
               afr_has_quorum(data_readable, this, NULL)) {
        /* read_subvol is guaranteed to be valid if we hit this path. */
        *read_subvol = afr_first_up_child(frame, this);
    } else {
        /* If quorum is enabled and we do not have a
           readable yet, it means all good copies are down.
        */
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        gf_msg(this->name, GF_LOG_WARNING, 0, AFR_MSG_READ_SUBVOL_ERROR,
               "no read "
               "subvols for %s",
               local->loc.path);
    }
    if (*read_subvol >= 0)
        dict_del_sizen(local->replies[*read_subvol].xdata, GF_CONTENT_KEY);
}

static void
afr_lookup_done(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = -1;
    int op_errno = 0;
    int read_subvol = 0;
    int par_read_subvol = 0;
    int ret = -1;
    unsigned char *readable = NULL;
    unsigned char *success_replies = NULL;
    int event = 0;
    struct afr_reply *replies = NULL;
    uuid_t read_gfid = {
        0,
    };
    gf_boolean_t locked_entry = _gf_false;
    gf_boolean_t in_flight_create = _gf_false;
    gf_boolean_t can_interpret = _gf_true;
    inode_t *parent = NULL;
    ia_type_t ia_type = IA_INVAL;
    afr_read_subvol_args_t args = {
        0,
    };
    char *gfid_heal_msg = NULL;

    priv = this->private;
    local = frame->local;
    replies = local->replies;
    parent = local->loc.parent;

    locked_entry = afr_is_possibly_under_txn(AFR_ENTRY_TRANSACTION, local,
                                             this);

    readable = alloca0(priv->child_count);
    success_replies = alloca0(priv->child_count);

    afr_inode_read_subvol_get(parent, this, readable, NULL, &event);
    par_read_subvol = afr_get_parent_read_subvol(this, parent, replies,
                                                 readable);

    /* First, check if we have a gfid-change from somewhere,
       If so, propagate that so that a fresh lookup can be
       issued
    */
    if (local->cont.lookup.needs_fresh_lookup) {
        local->op_ret = -1;
        local->op_errno = ESTALE;
        goto error;
    }

    op_errno = afr_final_errno(frame->local, this->private);
    local->op_errno = op_errno;

    read_subvol = -1;
    afr_fill_success_replies(local, priv, success_replies);

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid)
            continue;

        if (replies[i].op_ret == -1) {
            if (locked_entry && replies[i].op_errno == ENOENT) {
                in_flight_create = _gf_true;
            }
            continue;
        }

        if (read_subvol == -1 || !readable[read_subvol]) {
            read_subvol = i;
            gf_uuid_copy(read_gfid, replies[i].poststat.ia_gfid);
            ia_type = replies[i].poststat.ia_type;
            local->op_ret = 0;
        }
    }

    if (in_flight_create && !afr_has_quorum(success_replies, this, NULL)) {
        local->op_ret = -1;
        local->op_errno = ENOENT;
        goto error;
    }

    if (read_subvol == -1)
        goto error;
    /* We now have a read_subvol, which is readable[] (if there
       were any). Next we look for GFID mismatches. We don't
       consider a GFID mismatch as an error if read_subvol is
       readable[] but the mismatching GFID subvol is not.
    */
    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid || replies[i].op_ret == -1) {
            continue;
        }

        if (!gf_uuid_compare(replies[i].poststat.ia_gfid, read_gfid))
            continue;

        can_interpret = _gf_false;

        if (locked_entry)
            continue;

        /* Now GFIDs mismatch. It's OK as long as this subvol
           is not readable[] but read_subvol is */
        if (readable[read_subvol] && !readable[i])
            continue;

        /* If we were called from glfsheal and there is still a gfid
         * mismatch, succeed the lookup and let glfsheal print the
         * response via gfid-heal-msg.*/
        if (!dict_get_str_sizen(local->xattr_rsp, "gfid-heal-msg",
                                &gfid_heal_msg))
            goto cant_interpret;

        /* LOG ERROR */
        local->op_ret = -1;
        local->op_errno = EIO;
        goto error;
    }

    /* Forth, for the finalized GFID, pick the best subvolume
       to return stats from.
    */
    read_subvol = -1;
    memset(readable, 0, sizeof(*readable) * priv->child_count);
    if (can_interpret) {
        if (!afr_has_quorum(success_replies, this, NULL))
            goto cant_interpret;
        /* It is safe to call afr_replies_interpret() because we have
           a response from all the UP subvolumes and all of them resolved
           to the same GFID
        */
        gf_uuid_copy(args.gfid, read_gfid);
        args.ia_type = ia_type;
        ret = afr_replies_interpret(frame, this, local->inode, NULL);
        read_subvol = afr_read_subvol_decide(local->inode, this, &args,
                                             readable);
        if (read_subvol == -1)
            goto cant_interpret;
        if (ret) {
            afr_inode_need_refresh_set(local->inode, this);
            dict_del_sizen(local->replies[read_subvol].xdata, GF_CONTENT_KEY);
        }
    } else {
    cant_interpret:
        afr_attempt_readsubvol_set(frame, this, success_replies, readable,
                                   &read_subvol);
        if (read_subvol == -1) {
            goto error;
        }
    }

    afr_handle_quota_size(frame, this);

    afr_set_need_heal(this, local);
    if (AFR_IS_ARBITER_BRICK(priv, read_subvol) && local->op_ret == 0) {
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        gf_msg_debug(this->name, 0,
                     "Arbiter cannot be a read subvol "
                     "for %s",
                     local->loc.path);
        goto error;
    }

    ret = dict_get_str_sizen(local->xattr_rsp, "gfid-heal-msg", &gfid_heal_msg);
    if (!ret) {
        ret = dict_set_str_sizen(local->replies[read_subvol].xdata,
                                 "gfid-heal-msg", gfid_heal_msg);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_DICT_SET_FAILED,
                   "Error setting gfid-heal-msg dict");
            local->op_ret = -1;
            local->op_errno = ENOMEM;
        }
    }

    AFR_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                     local->inode, &local->replies[read_subvol].poststat,
                     local->replies[read_subvol].xdata,
                     &local->replies[par_read_subvol].postparent);
    return;

error:
    AFR_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno, NULL, NULL,
                     NULL, NULL);
}

/*
 * During a lookup, some errors are more "important" than
 * others in that they must be given higher priority while
 * returning to the user.
 *
 * The hierarchy is ENODATA > ENOENT > ESTALE > ENOSPC others
 */

int
afr_higher_errno(int32_t old_errno, int32_t new_errno)
{
    if (old_errno == ENODATA || new_errno == ENODATA)
        return ENODATA;
    if (old_errno == ENOENT || new_errno == ENOENT)
        return ENOENT;
    if (old_errno == ESTALE || new_errno == ESTALE)
        return ESTALE;
    if (old_errno == ENOSPC || new_errno == ENOSPC)
        return ENOSPC;

    return new_errno;
}

int
afr_final_errno(afr_local_t *local, afr_private_t *priv)
{
    int i = 0;
    int op_errno = 0;
    int tmp_errno = 0;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;
        if (local->replies[i].op_ret >= 0)
            continue;
        tmp_errno = local->replies[i].op_errno;
        op_errno = afr_higher_errno(op_errno, tmp_errno);
    }

    return op_errno;
}

static int32_t
afr_local_discovery_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *dict,
                        dict_t *xdata)
{
    int ret = 0;
    char *pathinfo = NULL;
    gf_boolean_t is_local = _gf_false;
    afr_private_t *priv = NULL;
    int32_t child_index = -1;

    if (op_ret != 0) {
        goto out;
    }

    priv = this->private;
    child_index = (int32_t)(long)cookie;

    ret = dict_get_str_sizen(dict, GF_XATTR_PATHINFO_KEY, &pathinfo);
    if (ret != 0) {
        goto out;
    }

    ret = glusterfs_is_local_pathinfo(pathinfo, &is_local);
    if (ret) {
        goto out;
    }

    /*
     * Note that one local subvolume will override another here.  The only
     * way to avoid that would be to retain extra information about whether
     * the previous read_child is local, and it's just not worth it.  Even
     * the slowest local subvolume is far preferable to a remote one.
     */
    if (is_local) {
        priv->local[child_index] = 1;
        /* Don't set arbiter as read child. */
        if (AFR_IS_ARBITER_BRICK(priv, child_index))
            goto out;
        gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_LOCAL_CHILD,
               "selecting local read_child %s",
               priv->children[child_index]->name);

        priv->read_child = child_index;
    }
out:
    STACK_DESTROY(frame->root);
    return 0;
}

static void
afr_attempt_local_discovery(xlator_t *this, int32_t child_index)
{
    call_frame_t *newframe = NULL;
    loc_t tmploc = {
        0,
    };
    afr_private_t *priv = this->private;

    newframe = create_frame(this, this->ctx->pool);
    if (!newframe) {
        return;
    }

    tmploc.gfid[sizeof(tmploc.gfid) - 1] = 1;
    STACK_WIND_COOKIE(newframe, afr_local_discovery_cbk,
                      (void *)(long)child_index, priv->children[child_index],
                      priv->children[child_index]->fops->getxattr, &tmploc,
                      GF_XATTR_PATHINFO_KEY, NULL);
}

int
afr_lookup_sh_metadata_wrap(void *opaque)
{
    call_frame_t *frame = opaque;
    afr_local_t *local = NULL;
    xlator_t *this = NULL;
    inode_t *inode = NULL;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;
    int i = 0, first = -1;
    int ret = -1;
    dict_t *dict = NULL;

    local = frame->local;
    this = frame->this;
    priv = this->private;
    replies = local->replies;

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid || replies[i].op_ret == -1)
            continue;
        first = i;
        break;
    }
    if (first == -1)
        goto out;

    if (afr_selfheal_metadata_by_stbuf(this, &replies[first].poststat))
        goto out;

    afr_local_replies_wipe(local, this->private);

    dict = dict_new();
    if (!dict)
        goto out;
    if (local->xattr_req) {
        dict_copy(local->xattr_req, dict);
    }

    ret = dict_set_sizen_str_sizen(dict, "link-count", GF_XATTROP_INDEX_COUNT);
    if (ret) {
        gf_msg_debug(this->name, -ret, "Unable to set link-count in dict ");
    }

    if (loc_is_nameless(&local->loc)) {
        ret = afr_selfheal_unlocked_discover_on(frame, local->inode,
                                                local->loc.gfid, local->replies,
                                                local->child_up, dict);
    } else {
        inode = afr_selfheal_unlocked_lookup_on(frame, local->loc.parent,
                                                local->loc.name, local->replies,
                                                local->child_up, dict);
    }
    if (inode)
        inode_unref(inode);
out:
    if (loc_is_nameless(&local->loc))
        afr_discover_done(frame, this);
    else
        afr_lookup_done(frame, this);

    if (dict)
        dict_unref(dict);

    return 0;
}

gf_boolean_t
afr_is_pending_set(xlator_t *this, dict_t *xdata, int type)
{
    int idx = -1;
    afr_private_t *priv = NULL;
    void *pending_raw = NULL;
    int *pending_int = NULL;
    int i = 0;

    priv = this->private;
    idx = afr_index_for_transaction_type(type);

    if (dict_get_ptr(xdata, AFR_DIRTY, &pending_raw) == 0) {
        if (pending_raw) {
            pending_int = pending_raw;

            if (ntoh32(pending_int[idx]))
                return _gf_true;
        }
    }

    for (i = 0; i < priv->child_count; i++) {
        if (dict_get_ptr(xdata, priv->pending_key[i], &pending_raw))
            continue;
        if (!pending_raw)
            continue;
        pending_int = pending_raw;

        if (ntoh32(pending_int[idx]))
            return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
afr_can_start_metadata_self_heal(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;
    int i = 0, first = -1;
    gf_boolean_t start = _gf_false;
    struct iatt stbuf = {
        0,
    };

    local = frame->local;
    replies = local->replies;
    priv = this->private;

    if (!priv->metadata_self_heal)
        return _gf_false;

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid || replies[i].op_ret == -1)
            continue;
        if (first == -1) {
            first = i;
            stbuf = replies[i].poststat;
            continue;
        }

        if (afr_is_pending_set(this, replies[i].xdata,
                               AFR_METADATA_TRANSACTION)) {
            /* Let shd do the heal so that lookup is not blocked
             * on getting metadata lock/doing the heal */
            start = _gf_false;
            break;
        }

        if (gf_uuid_compare(stbuf.ia_gfid, replies[i].poststat.ia_gfid)) {
            start = _gf_false;
            break;
        }
        if (!IA_EQUAL(stbuf, replies[i].poststat, type)) {
            start = _gf_false;
            break;
        }

        /*Check if iattrs need heal*/
        if ((!IA_EQUAL(stbuf, replies[i].poststat, uid)) ||
            (!IA_EQUAL(stbuf, replies[i].poststat, gid)) ||
            (!IA_EQUAL(stbuf, replies[i].poststat, prot))) {
            start = _gf_true;
            continue;
        }

        /*Check if xattrs need heal*/
        if (!afr_xattrs_are_equal(replies[first].xdata, replies[i].xdata))
            start = _gf_true;
    }

    return start;
}

int
afr_lookup_metadata_heal_check(call_frame_t *frame, xlator_t *this)

{
    call_frame_t *heal = NULL;
    afr_local_t *local = NULL;
    int ret = 0;

    local = frame->local;
    if (!afr_can_start_metadata_self_heal(frame, this))
        goto out;

    heal = afr_frame_create(this, &ret);
    if (!heal) {
        ret = -ret;
        goto out;
    }

    ret = synctask_new(this->ctx->env, afr_lookup_sh_metadata_wrap,
                       afr_refresh_selfheal_done, heal, frame);
    if (ret)
        goto out;
    return ret;
out:
    if (loc_is_nameless(&local->loc))
        afr_discover_done(frame, this);
    else
        afr_lookup_done(frame, this);
    if (heal)
        AFR_STACK_DESTROY(heal);
    return ret;
}

int
afr_lookup_selfheal_wrap(void *opaque)
{
    int ret = 0;
    call_frame_t *frame = opaque;
    afr_local_t *local = NULL;
    xlator_t *this = NULL;
    inode_t *inode = NULL;
    uuid_t pargfid = {
        0,
    };

    local = frame->local;
    this = frame->this;
    loc_pargfid(&local->loc, pargfid);
    if (!local->xattr_rsp)
        local->xattr_rsp = dict_new();

    ret = afr_selfheal_name(frame->this, pargfid, local->loc.name,
                            &local->cont.lookup.gfid_req, local->xattr_req,
                            local->xattr_rsp);
    if (ret == -EIO)
        goto unwind;

    afr_local_replies_wipe(local, this->private);

    inode = afr_selfheal_unlocked_lookup_on(frame, local->loc.parent,
                                            local->loc.name, local->replies,
                                            local->child_up, local->xattr_req);
    if (inode)
        inode_unref(inode);

    afr_lookup_metadata_heal_check(frame, this);
    return 0;

unwind:
    AFR_STACK_UNWIND(lookup, frame, -1, EIO, NULL, NULL, local->xattr_rsp,
                     NULL);
    return 0;
}

int
afr_lookup_entry_heal(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    call_frame_t *heal = NULL;
    int i = 0, first = -1;
    gf_boolean_t name_state_mismatch = _gf_false;
    struct afr_reply *replies = NULL;
    int ret = 0;
    unsigned char *par_readables = NULL;
    unsigned char *success = NULL;
    int32_t op_errno = 0;
    uuid_t gfid = {0};

    local = frame->local;
    replies = local->replies;
    priv = this->private;
    par_readables = alloca0(priv->child_count);
    success = alloca0(priv->child_count);

    ret = afr_inode_read_subvol_get(local->loc.parent, this, par_readables,
                                    NULL, NULL);
    if (ret < 0 || AFR_COUNT(par_readables, priv->child_count) == 0) {
        /* In this case set par_readables to all 1 so that name_heal
         * need checks at the end of this function will flag missing
         * entry when name state mismatches*/
        memset(par_readables, 1, priv->child_count);
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!replies[i].valid)
            continue;

        if (replies[i].op_ret == 0) {
            if (gf_uuid_is_null(gfid)) {
                gf_uuid_copy(gfid, replies[i].poststat.ia_gfid);
            }
            success[i] = 1;
        } else {
            if ((replies[i].op_errno != ENOTCONN) &&
                (replies[i].op_errno != ENOENT) &&
                (replies[i].op_errno != ESTALE)) {
                op_errno = replies[i].op_errno;
            }
        }

        /*gfid is missing, needs heal*/
        if ((replies[i].op_ret == -1) && (replies[i].op_errno == ENODATA)) {
            goto name_heal;
        }

        if (first == -1) {
            first = i;
            continue;
        }

        if (replies[i].op_ret != replies[first].op_ret) {
            name_state_mismatch = _gf_true;
        }

        if (replies[i].op_ret == 0) {
            /* Rename after this lookup may succeed if we don't do
             * a name-heal and the destination may not have pending xattrs
             * to indicate which name is good and which is bad so always do
             * this heal*/
            if (gf_uuid_compare(replies[i].poststat.ia_gfid, gfid)) {
                goto name_heal;
            }
        }
    }

    if (name_state_mismatch) {
        if (!priv->quorum_count)
            goto name_heal;
        if (!afr_has_quorum(success, this, NULL))
            goto name_heal;
        if (op_errno)
            goto name_heal;
        for (i = 0; i < priv->child_count; i++) {
            if (!replies[i].valid)
                continue;
            if (par_readables[i] && replies[i].op_ret < 0 &&
                replies[i].op_errno != ENOTCONN) {
                goto name_heal;
            }
        }
    }

    goto metadata_heal;

name_heal:
    heal = afr_frame_create(this, NULL);
    if (!heal)
        goto metadata_heal;

    ret = synctask_new(this->ctx->env, afr_lookup_selfheal_wrap,
                       afr_refresh_selfheal_done, heal, frame);
    if (ret) {
        AFR_STACK_DESTROY(heal);
        goto metadata_heal;
    }
    return ret;

metadata_heal:
    ret = afr_lookup_metadata_heal_check(frame, this);

    return ret;
}

int
afr_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, inode_t *inode, struct iatt *buf, dict_t *xdata,
               struct iatt *postparent)
{
    afr_local_t *local = NULL;
    int call_count = -1;
    int child_index = -1;
    GF_UNUSED int ret = 0;
    int8_t need_heal = 1;

    child_index = (long)cookie;

    local = frame->local;

    local->replies[child_index].valid = 1;
    local->replies[child_index].op_ret = op_ret;
    local->replies[child_index].op_errno = op_errno;
    /*
     * On revalidate lookup if the gfid-changed, afr should unwind the fop
     * with ESTALE so that a fresh lookup will be sent by the top xlator.
     * So remember it.
     */
    if (xdata && dict_get_sizen(xdata, "gfid-changed"))
        local->cont.lookup.needs_fresh_lookup = _gf_true;

    if (xdata) {
        ret = dict_get_int8(xdata, "link-count", &need_heal);
        local->replies[child_index].need_heal = need_heal;
    } else {
        local->replies[child_index].need_heal = need_heal;
    }
    if (op_ret != -1) {
        local->replies[child_index].poststat = *buf;
        local->replies[child_index].postparent = *postparent;
        if (xdata)
            local->replies[child_index].xdata = dict_ref(xdata);
    }

    call_count = afr_frame_return(frame);
    if (call_count == 0) {
        afr_set_need_heal(this, local);
        afr_lookup_entry_heal(frame, this);
    }

    return 0;
}

static void
afr_discover_unwind(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int read_subvol = -1;
    int ret = 0;
    unsigned char *data_readable = NULL;
    unsigned char *success_replies = NULL;

    priv = this->private;
    local = frame->local;
    data_readable = alloca0(priv->child_count);
    success_replies = alloca0(priv->child_count);

    afr_fill_success_replies(local, priv, success_replies);
    if (AFR_COUNT(success_replies, priv->child_count) > 0)
        local->op_ret = 0;

    if (local->op_ret < 0) {
        local->op_ret = -1;
        local->op_errno = afr_final_errno(frame->local, this->private);
        goto error;
    }

    if (!afr_has_quorum(success_replies, this, frame))
        goto unwind;

    ret = afr_replies_interpret(frame, this, local->inode, NULL);
    if (ret) {
        afr_inode_need_refresh_set(local->inode, this);
    }

    read_subvol = afr_read_subvol_decide(local->inode, this, NULL,
                                         data_readable);

unwind:
    afr_attempt_readsubvol_set(frame, this, success_replies, data_readable,
                               &read_subvol);
    if (read_subvol == -1)
        goto error;

    if (AFR_IS_ARBITER_BRICK(priv, read_subvol) && local->op_ret == 0) {
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        gf_msg_debug(this->name, 0,
                     "Arbiter cannot be a read subvol "
                     "for %s",
                     local->loc.path);
    }

    AFR_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno,
                     local->inode, &local->replies[read_subvol].poststat,
                     local->replies[read_subvol].xdata,
                     &local->replies[read_subvol].postparent);
    return;

error:
    AFR_STACK_UNWIND(lookup, frame, local->op_ret, local->op_errno, NULL, NULL,
                     NULL, NULL);
}

static int
afr_ta_id_file_check(void *opaque)
{
    afr_private_t *priv = NULL;
    xlator_t *this = NULL;
    loc_t loc = {
        0,
    };
    struct iatt stbuf = {
        0,
    };
    dict_t *dict = NULL;
    uuid_t gfid = {
        0,
    };
    fd_t *fd = NULL;
    int ret = 0;

    this = opaque;
    priv = this->private;

    ret = afr_fill_ta_loc(this, &loc, _gf_false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate thin-arbiter loc for: %s.", loc.name);
        goto out;
    }

    ret = syncop_lookup(priv->children[THIN_ARBITER_BRICK_INDEX], &loc, &stbuf,
                        0, 0, 0);
    if (ret == 0) {
        goto out;
    } else if (ret == -ENOENT) {
        fd = fd_create(loc.inode, getpid());
        if (!fd)
            goto out;
        dict = dict_new();
        if (!dict)
            goto out;
        gf_uuid_generate(gfid);
        ret = dict_set_gfuuid(dict, "gfid-req", gfid, true);
        ret = syncop_create(priv->children[THIN_ARBITER_BRICK_INDEX], &loc,
                            O_RDWR, 0664, fd, &stbuf, dict, NULL);
    }

out:
    if (ret == 0) {
        gf_uuid_copy(priv->ta_gfid, stbuf.ia_gfid);
    } else {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to lookup/create thin-arbiter id file.");
    }
    if (dict)
        dict_unref(dict);
    if (fd)
        fd_unref(fd);
    loc_wipe(&loc);

    return 0;
}

static int
afr_ta_id_file_check_cbk(int ret, call_frame_t *ta_frame, void *opaque)
{
    return 0;
}

static void
afr_discover_done(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    afr_private_t *priv = NULL;

    priv = this->private;
    if (!priv->thin_arbiter_count)
        goto unwind;
    if (!gf_uuid_is_null(priv->ta_gfid))
        goto unwind;

    ret = synctask_new(this->ctx->env, afr_ta_id_file_check,
                       afr_ta_id_file_check_cbk, NULL, this);
    if (ret)
        goto unwind;
unwind:
    afr_discover_unwind(frame, this);
}

int
afr_discover_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, inode_t *inode, struct iatt *buf, dict_t *xdata,
                 struct iatt *postparent)
{
    afr_local_t *local = NULL;
    int call_count = -1;
    int child_index = -1;
    GF_UNUSED int ret = 0;
    int8_t need_heal = 1;

    child_index = (long)cookie;

    local = frame->local;

    local->replies[child_index].valid = 1;
    local->replies[child_index].op_ret = op_ret;
    local->replies[child_index].op_errno = op_errno;
    if (op_ret != -1) {
        local->replies[child_index].poststat = *buf;
        local->replies[child_index].postparent = *postparent;
        if (xdata)
            local->replies[child_index].xdata = dict_ref(xdata);
    }

    if (local->do_discovery && (op_ret == 0))
        afr_attempt_local_discovery(this, child_index);

    if (xdata) {
        ret = dict_get_int8(xdata, "link-count", &need_heal);
        local->replies[child_index].need_heal = need_heal;
    } else {
        local->replies[child_index].need_heal = need_heal;
    }

    call_count = afr_frame_return(frame);
    if (call_count == 0) {
        afr_set_need_heal(this, local);
        afr_lookup_metadata_heal_check(frame, this);
    }

    return 0;
}

int
afr_discover_do(call_frame_t *frame, xlator_t *this, int err)
{
    int ret = 0;
    int i = 0;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = 0;

    local = frame->local;
    priv = this->private;

    if (err) {
        local->op_errno = err;
        goto out;
    }

    call_count = local->call_count = AFR_COUNT(local->child_up,
                                               priv->child_count);

    ret = afr_lookup_xattr_req_prepare(local, this, local->xattr_req,
                                       &local->loc);
    if (ret) {
        local->op_errno = -ret;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(
                frame, afr_discover_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->lookup, &local->loc, local->xattr_req);
            if (!--call_count)
                break;
        }
    }

    return 0;
out:
    AFR_STACK_UNWIND(lookup, frame, -1, local->op_errno, 0, 0, 0, 0);
    return 0;
}

int
afr_discover(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    int op_errno = ENOMEM;
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int event = 0;

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    if (!local->call_count) {
        op_errno = ENOTCONN;
        goto out;
    }

    if (__is_root_gfid(loc->inode->gfid)) {
        if (!priv->root_inode)
            priv->root_inode = inode_ref(loc->inode);

        if (priv->choose_local && !priv->did_discovery) {
            /* Logic to detect which subvolumes of AFR are
               local, in order to prefer them for reads
            */
            local->do_discovery = _gf_true;
            priv->did_discovery = _gf_true;
        }
    }

    local->op = GF_FOP_LOOKUP;

    loc_copy(&local->loc, loc);

    local->inode = inode_ref(loc->inode);

    if (xattr_req) {
        /* If xattr_req was null, afr_lookup_xattr_req_prepare() will
           allocate one for us */
        local->xattr_req = dict_copy_with_ref(xattr_req, NULL);
        if (!local->xattr_req) {
            op_errno = ENOMEM;
            goto out;
        }
    }

    if (gf_uuid_is_null(loc->inode->gfid)) {
        afr_discover_do(frame, this, 0);
        return 0;
    }

    afr_read_subvol_get(loc->inode, this, NULL, NULL, &event,
                        AFR_DATA_TRANSACTION, NULL);

    afr_discover_do(frame, this, 0);

    return 0;
out:
    AFR_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

int
afr_lookup_do(call_frame_t *frame, xlator_t *this, int err)
{
    int ret = 0;
    int i = 0;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = 0;

    local = frame->local;
    priv = this->private;

    if (err < 0) {
        local->op_errno = err;
        goto out;
    }

    call_count = local->call_count = AFR_COUNT(local->child_up,
                                               priv->child_count);

    ret = afr_lookup_xattr_req_prepare(local, this, local->xattr_req,
                                       &local->loc);
    if (ret) {
        local->op_errno = -ret;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(
                frame, afr_lookup_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->lookup, &local->loc, local->xattr_req);
            if (!--call_count)
                break;
        }
    }
    return 0;
out:
    AFR_STACK_UNWIND(lookup, frame, -1, local->op_errno, 0, 0, 0, 0);
    return 0;
}

/*
 * afr_lookup()
 *
 * The goal here is to figure out what the element getting looked up is.
 * i.e what is the GFID, inode type and a conservative estimate of the
 * inode attributes are.
 *
 * As we lookup, operations may be underway on the entry name and the
 * inode. In lookup() we are primarily concerned only with the entry
 * operations. If the entry is getting unlinked or renamed, we detect
 * what operation is underway by querying for on-going transactions and
 * pending self-healing on the entry through xdata.
 *
 * If the entry is a file/dir, it may need self-heal and/or in a
 * split-brain condition. Lookup is not the place to worry about these
 * conditions. Outcast marking will naturally handle them in the read
 * paths.
 *
 * Here is a brief goal of what we are trying to achieve:
 *
 * - LOOKUP on all subvolumes concurrently, querying on-going transaction
 *   and pending self-heal info from the servers.
 *
 * - If all servers reply the same inode type and GFID, the overall call
 *   MUST be a success.
 *
 * - If inode types or GFIDs mismatch, and there IS either an on-going
 *   transaction or pending self-heal, inspect what the nature of the
 *   transaction or pending heal is, and select the appropriate subvolume's
 *   reply as the winner.
 *
 * - If inode types or GFIDs mismatch, and there are no on-going transactions
 *   or pending self-heal on the entry name on any of the servers, fail the
 *   lookup with EIO. Something has gone wrong beyond reasonable action.
 */

int
afr_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    afr_local_t *local = NULL;
    int32_t op_errno = 0;
    int event = 0;
    int ret = 0;

    if (loc_is_nameless(loc)) {
        if (xattr_req)
            dict_del_sizen(xattr_req, "gfid-req");
        afr_discover(frame, this, loc, xattr_req);
        return 0;
    }

    if (afr_is_private_directory(this->private, loc->parent->gfid, loc->name,
                                 frame->root->pid)) {
        op_errno = EPERM;
        goto out;
    }

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    if (!local->call_count) {
        op_errno = ENOTCONN;
        goto out;
    }

    local->op = GF_FOP_LOOKUP;

    loc_copy(&local->loc, loc);

    local->inode = inode_ref(loc->inode);

    if (xattr_req) {
        /* If xattr_req was null, afr_lookup_xattr_req_prepare() will
           allocate one for us */
        local->xattr_req = dict_copy_with_ref(xattr_req, NULL);
        if (!local->xattr_req) {
            op_errno = ENOMEM;
            goto out;
        }
        ret = dict_get_gfuuid(local->xattr_req, "gfid-req",
                              &local->cont.lookup.gfid_req);
        if (ret == 0) {
            dict_del_sizen(local->xattr_req, "gfid-req");
        }
    }

    afr_read_subvol_get(loc->parent, this, NULL, NULL, &event,
                        AFR_DATA_TRANSACTION, NULL);

    afr_lookup_do(frame, this, 0);

    return 0;
out:
    AFR_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

    return 0;
}

void
_afr_cleanup_fd_ctx(xlator_t *this, afr_fd_ctx_t *fd_ctx)
{
    afr_private_t *priv = this->private;

    if (fd_ctx->lk_heal_info) {
        LOCK(&priv->lock);
        {
            list_del(&fd_ctx->lk_heal_info->pos);
        }
        afr_lk_heal_info_cleanup(fd_ctx->lk_heal_info);
        fd_ctx->lk_heal_info = NULL;
    }
    GF_FREE(fd_ctx->opened_on);
    GF_FREE(fd_ctx);
    return;
}

int
afr_cleanup_fd_ctx(xlator_t *this, fd_t *fd)
{
    uint64_t ctx = 0;
    afr_fd_ctx_t *fd_ctx = NULL;
    int ret = 0;

    ret = fd_ctx_get(fd, this, &ctx);
    if (ret < 0)
        goto out;

    fd_ctx = (afr_fd_ctx_t *)(long)ctx;

    if (fd_ctx) {
        _afr_cleanup_fd_ctx(this, fd_ctx);
    }

out:
    return 0;
}

int
afr_release(xlator_t *this, fd_t *fd)
{
    afr_cleanup_fd_ctx(this, fd);

    return 0;
}

afr_fd_ctx_t *
__afr_fd_ctx_get(fd_t *fd, xlator_t *this)
{
    uint64_t ctx = 0;
    int ret = 0;
    afr_fd_ctx_t *fd_ctx = NULL;

    ret = __fd_ctx_get(fd, this, &ctx);

    if (ret < 0) {
        ret = __afr_fd_ctx_set(this, fd);
        if (ret < 0)
            goto out;

        ret = __fd_ctx_get(fd, this, &ctx);
        if (ret < 0)
            goto out;
    }

    fd_ctx = (afr_fd_ctx_t *)(long)ctx;
out:
    return fd_ctx;
}

afr_fd_ctx_t *
afr_fd_ctx_get(fd_t *fd, xlator_t *this)
{
    afr_fd_ctx_t *fd_ctx = NULL;

    LOCK(&fd->lock);
    {
        fd_ctx = __afr_fd_ctx_get(fd, this);
    }
    UNLOCK(&fd->lock);

    return fd_ctx;
}

int
__afr_fd_ctx_set(xlator_t *this, fd_t *fd)
{
    afr_private_t *priv = NULL;
    int ret = -1;
    uint64_t ctx = 0;
    afr_fd_ctx_t *fd_ctx = NULL;
    int i = 0;

    VALIDATE_OR_GOTO(this->private, out);
    VALIDATE_OR_GOTO(fd, out);

    priv = this->private;

    ret = __fd_ctx_get(fd, this, &ctx);

    if (ret == 0)
        goto out;

    fd_ctx = GF_CALLOC(1, sizeof(afr_fd_ctx_t), gf_afr_mt_afr_fd_ctx_t);
    if (!fd_ctx) {
        ret = -ENOMEM;
        goto out;
    }

    fd_ctx->opened_on = GF_CALLOC(sizeof(*fd_ctx->opened_on), priv->child_count,
                                  gf_afr_mt_int32_t);
    if (!fd_ctx->opened_on) {
        ret = -ENOMEM;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (fd_is_anonymous(fd))
            fd_ctx->opened_on[i] = AFR_FD_OPENED;
        else
            fd_ctx->opened_on[i] = AFR_FD_NOT_OPENED;
    }

    fd_ctx->readdir_subvol = -1;
    fd_ctx->lk_heal_info = NULL;

    ret = __fd_ctx_set(fd, this, (uint64_t)(long)fd_ctx);
    if (ret)
        gf_msg_debug(this->name, 0, "failed to set fd ctx (%p)", fd);
out:
    if (ret && fd_ctx)
        _afr_cleanup_fd_ctx(this, fd_ctx);
    return ret;
}

/* {{{ flush */

int
afr_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = -1;

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (op_ret != -1) {
            local->op_ret = op_ret;
            if (!local->xdata_rsp && xdata)
                local->xdata_rsp = dict_ref(xdata);
        } else {
            local->op_errno = op_errno;
        }
        call_count = --local->call_count;
    }
    UNLOCK(&frame->lock);

    if (call_count == 0)
        AFR_STACK_UNWIND(flush, frame, local->op_ret, local->op_errno,
                         local->xdata_rsp);

    return 0;
}

static int
afr_flush_wrapper(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int i = 0;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = -1;

    priv = this->private;
    local = frame->local;
    call_count = local->call_count;

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(frame, afr_flush_cbk, (void *)(long)i,
                              priv->children[i], priv->children[i]->fops->flush,
                              local->fd, xdata);
            if (!--call_count)
                break;
        }
    }

    return 0;
}

afr_local_t *
afr_wakeup_same_fd_delayed_op(xlator_t *this, afr_lock_t *lock, fd_t *fd)
{
    afr_local_t *local = NULL;

    if (lock->delay_timer) {
        local = list_entry(lock->post_op.next, afr_local_t,
                           transaction.owner_list);
        if (fd == local->fd) {
            if (gf_timer_call_cancel(this->ctx, lock->delay_timer)) {
                local = NULL;
            } else {
                lock->delay_timer = NULL;
            }
        } else {
            local = NULL;
        }
    }

    return local;
}

void
afr_delayed_changelog_wake_resume(xlator_t *this, inode_t *inode,
                                  call_stub_t *stub)
{
    afr_inode_ctx_t *ctx = NULL;
    afr_lock_t *lock = NULL;
    afr_local_t *metadata_local = NULL;
    afr_local_t *data_local = NULL;
    LOCK(&inode->lock);
    {
        (void)__afr_inode_ctx_get(this, inode, &ctx);
        lock = &ctx->lock[AFR_DATA_TRANSACTION];
        data_local = afr_wakeup_same_fd_delayed_op(this, lock, stub->args.fd);
        lock = &ctx->lock[AFR_METADATA_TRANSACTION];
        metadata_local = afr_wakeup_same_fd_delayed_op(this, lock,
                                                       stub->args.fd);
    }
    UNLOCK(&inode->lock);

    if (data_local) {
        data_local->transaction.resume_stub = stub;
    } else if (metadata_local) {
        metadata_local->transaction.resume_stub = stub;
    } else {
        call_resume(stub);
    }
    if (data_local) {
        afr_delayed_changelog_wake_up_cbk(data_local);
    }
    if (metadata_local) {
        afr_delayed_changelog_wake_up_cbk(metadata_local);
    }
}

int
afr_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    afr_local_t *local = NULL;
    call_stub_t *stub = NULL;
    int op_errno = ENOMEM;

    AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_FLUSH;
    if (!afr_is_consistent_io_possible(local, this->private, &op_errno))
        goto out;

    local->fd = fd_ref(fd);

    stub = fop_flush_stub(frame, afr_flush_wrapper, fd, xdata);
    if (!stub)
        goto out;

    afr_delayed_changelog_wake_resume(this, fd->inode, stub);

    return 0;
out:
    AFR_STACK_UNWIND(flush, frame, -1, op_errno, NULL);
    return 0;
}

int
afr_fsyncdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = -1;

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (op_ret == 0) {
            local->op_ret = 0;
            if (!local->xdata_rsp && xdata)
                local->xdata_rsp = dict_ref(xdata);
        } else {
            local->op_errno = op_errno;
        }
        call_count = --local->call_count;
    }
    UNLOCK(&frame->lock);

    if (call_count == 0)
        AFR_STACK_UNWIND(fsyncdir, frame, local->op_ret, local->op_errno,
                         local->xdata_rsp);

    return 0;
}

int
afr_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
             dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;
    int32_t call_count = 0;
    int32_t op_errno = ENOMEM;

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_FSYNCDIR;
    if (!afr_is_consistent_io_possible(local, priv, &op_errno))
        goto out;

    call_count = local->call_count;
    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND(frame, afr_fsyncdir_cbk, priv->children[i],
                       priv->children[i]->fops->fsyncdir, fd, datasync, xdata);
            if (!--call_count)
                break;
        }
    }

    return 0;
out:
    AFR_STACK_UNWIND(fsyncdir, frame, -1, op_errno, NULL);

    return 0;
}

/* }}} */

static int
afr_serialized_lock_wind(call_frame_t *frame, xlator_t *this);

static gf_boolean_t
afr_is_conflicting_lock_present(int32_t op_ret, int32_t op_errno)
{
    if (op_ret == -1 && op_errno == EAGAIN)
        return _gf_true;
    return _gf_false;
}

static void
afr_fop_lock_unwind(call_frame_t *frame, glusterfs_fop_t op, int32_t op_ret,
                    int32_t op_errno, dict_t *xdata)
{
    switch (op) {
        case GF_FOP_INODELK:
            AFR_STACK_UNWIND(inodelk, frame, op_ret, op_errno, xdata);
            break;
        case GF_FOP_FINODELK:
            AFR_STACK_UNWIND(finodelk, frame, op_ret, op_errno, xdata);
            break;
        case GF_FOP_ENTRYLK:
            AFR_STACK_UNWIND(entrylk, frame, op_ret, op_errno, xdata);
            break;
        case GF_FOP_FENTRYLK:
            AFR_STACK_UNWIND(fentrylk, frame, op_ret, op_errno, xdata);
            break;
        default:
            break;
    }
}

static void
afr_fop_lock_wind(call_frame_t *frame, xlator_t *this, int child_index,
                  int32_t (*lock_cbk)(call_frame_t *, void *, xlator_t *,
                                      int32_t, int32_t, dict_t *))
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = this->private;
    int i = child_index;

    switch (local->op) {
        case GF_FOP_INODELK:
            STACK_WIND_COOKIE(
                frame, lock_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->inodelk,
                (const char *)local->cont.inodelk.volume, &local->loc,
                local->cont.inodelk.cmd, &local->cont.inodelk.flock,
                local->cont.inodelk.xdata);
            break;
        case GF_FOP_FINODELK:
            STACK_WIND_COOKIE(
                frame, lock_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->finodelk,
                (const char *)local->cont.inodelk.volume, local->fd,
                local->cont.inodelk.cmd, &local->cont.inodelk.flock,
                local->cont.inodelk.xdata);
            break;
        case GF_FOP_ENTRYLK:
            STACK_WIND_COOKIE(
                frame, lock_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->entrylk, local->cont.entrylk.volume,
                &local->loc, local->cont.entrylk.basename,
                local->cont.entrylk.cmd, local->cont.entrylk.type,
                local->cont.entrylk.xdata);
            break;
        case GF_FOP_FENTRYLK:
            STACK_WIND_COOKIE(
                frame, lock_cbk, (void *)(long)i, priv->children[i],
                priv->children[i]->fops->fentrylk, local->cont.entrylk.volume,
                local->fd, local->cont.entrylk.basename,
                local->cont.entrylk.cmd, local->cont.entrylk.type,
                local->cont.entrylk.xdata);
            break;
        default:
            break;
    }
}

void
afr_fop_lock_proceed(call_frame_t *frame)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = frame->this->private;

    if (local->fop_lock_state != AFR_FOP_LOCK_PARALLEL) {
        afr_fop_lock_unwind(frame, local->op, local->op_ret, local->op_errno,
                            local->xdata_rsp);
        return;
    }
    /* At least one child is up */
    /*
     * Non-blocking locks also need to be serialized.  Otherwise there is
     * a chance that both the mounts which issued same non-blocking inodelk
     * may endup not acquiring the lock on any-brick.
     * Ex: Mount1 and Mount2
     * request for full length lock on file f1.  Mount1 afr may acquire the
     * partial lock on brick-1 and may not acquire the lock on brick-2
     * because Mount2 already got the lock on brick-2, vice versa.  Since
     * both the mounts only got partial locks, afr treats them as failure in
     * gaining the locks and unwinds with EAGAIN errno.
     */
    local->op_ret = -1;
    local->op_errno = EUCLEAN;
    local->fop_lock_state = AFR_FOP_LOCK_SERIAL;
    afr_local_replies_wipe(local, priv);
    if (local->xdata_rsp)
        dict_unref(local->xdata_rsp);
    local->xdata_rsp = NULL;
    switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
            local->cont.inodelk.cmd = local->cont.inodelk.in_cmd;
            local->cont.inodelk.flock = local->cont.inodelk.in_flock;
            if (local->cont.inodelk.xdata)
                dict_unref(local->cont.inodelk.xdata);
            local->cont.inodelk.xdata = NULL;
            if (local->xdata_req)
                local->cont.inodelk.xdata = dict_ref(local->xdata_req);
            break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
            local->cont.entrylk.cmd = local->cont.entrylk.in_cmd;
            if (local->cont.entrylk.xdata)
                dict_unref(local->cont.entrylk.xdata);
            local->cont.entrylk.xdata = NULL;
            if (local->xdata_req)
                local->cont.entrylk.xdata = dict_ref(local->xdata_req);
            break;
        default:
            break;
    }
    afr_serialized_lock_wind(frame, frame->this);
}

static int32_t
afr_unlock_partial_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = -1;
    int child_index = (long)cookie;
    uuid_t gfid = {0};

    local = frame->local;
    priv = this->private;

    if (op_ret < 0 && op_errno != ENOTCONN) {
        if (local->fd)
            gf_uuid_copy(gfid, local->fd->inode->gfid);
        else
            loc_gfid(&local->loc, gfid);
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_UNLOCK_FAIL,
               "%s: Failed to unlock %s on %s "
               "with lk_owner: %s",
               uuid_utoa(gfid), gf_fop_list[local->op],
               priv->children[child_index]->name,
               lkowner_utoa(&frame->root->lk_owner));
    }

    call_count = afr_frame_return(frame);
    if (call_count == 0)
        afr_fop_lock_proceed(frame);

    return 0;
}

static int32_t
afr_unlock_locks_and_proceed(call_frame_t *frame, xlator_t *this,
                             int call_count)
{
    int i = 0;
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;

    if (call_count == 0) {
        afr_fop_lock_proceed(frame);
        goto out;
    }

    local = frame->local;
    priv = this->private;
    local->call_count = call_count;
    switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
            local->cont.inodelk.flock.l_type = F_UNLCK;
            local->cont.inodelk.cmd = F_SETLK;
            if (local->cont.inodelk.xdata)
                dict_unref(local->cont.inodelk.xdata);
            local->cont.inodelk.xdata = NULL;
            break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
            local->cont.entrylk.cmd = ENTRYLK_UNLOCK;
            if (local->cont.entrylk.xdata)
                dict_unref(local->cont.entrylk.xdata);
            local->cont.entrylk.xdata = NULL;
            break;
        default:
            break;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;

        if (local->replies[i].op_ret == -1)
            continue;

        afr_fop_lock_wind(frame, this, i, afr_unlock_partial_lock_cbk);

        if (!--call_count)
            break;
    }

out:
    return 0;
}

int32_t
afr_fop_lock_done(call_frame_t *frame, xlator_t *this)
{
    int i = 0;
    int lock_count = 0;
    unsigned char *success = NULL;

    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;
    success = alloca0(priv->child_count);

    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;

        if (local->replies[i].op_ret == 0) {
            lock_count++;
            success[i] = 1;
        }

        if (local->op_ret == -1 && local->op_errno == EAGAIN)
            continue;

        if ((local->replies[i].op_ret == -1) &&
            (local->replies[i].op_errno == EAGAIN)) {
            local->op_ret = -1;
            local->op_errno = EAGAIN;
            continue;
        }

        if (local->replies[i].op_ret == 0)
            local->op_ret = 0;

        local->op_errno = local->replies[i].op_errno;
    }

    if (afr_fop_lock_is_unlock(frame))
        goto unwind;

    if (afr_is_conflicting_lock_present(local->op_ret, local->op_errno)) {
        afr_unlock_locks_and_proceed(frame, this, lock_count);
    } else if (priv->quorum_count && !afr_has_quorum(success, this, NULL)) {
        local->fop_lock_state = AFR_FOP_LOCK_QUORUM_FAILED;
        local->op_ret = -1;
        local->op_errno = afr_final_errno(local, priv);
        if (local->op_errno == 0)
            local->op_errno = afr_quorum_errno(priv);
        afr_unlock_locks_and_proceed(frame, this, lock_count);
    } else {
        goto unwind;
    }

    return 0;
unwind:
    afr_fop_lock_unwind(frame, local->op, local->op_ret, local->op_errno,
                        local->xdata_rsp);
    return 0;
}

static int
afr_common_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int child_index = (long)cookie;

    local = frame->local;

    local->replies[child_index].valid = 1;
    local->replies[child_index].op_ret = op_ret;
    local->replies[child_index].op_errno = op_errno;
    if (op_ret == 0 && xdata) {
        local->replies[child_index].xdata = dict_ref(xdata);
        LOCK(&frame->lock);
        {
            if (!local->xdata_rsp)
                local->xdata_rsp = dict_ref(xdata);
        }
        UNLOCK(&frame->lock);
    }
    return 0;
}

static int32_t
afr_serialized_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int child_index = (long)cookie;
    int next_child = 0;

    local = frame->local;
    priv = this->private;

    afr_common_lock_cbk(frame, cookie, this, op_ret, op_errno, xdata);

    for (next_child = child_index + 1; next_child < priv->child_count;
         next_child++) {
        if (local->child_up[next_child])
            break;
    }

    if (afr_is_conflicting_lock_present(op_ret, op_errno) ||
        (next_child == priv->child_count)) {
        afr_fop_lock_done(frame, this);
    } else {
        afr_fop_lock_wind(frame, this, next_child, afr_serialized_lock_cbk);
    }

    return 0;
}

static int
afr_serialized_lock_wind(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int i = 0;

    priv = this->private;
    local = frame->local;

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            afr_fop_lock_wind(frame, this, i, afr_serialized_lock_cbk);
            break;
        }
    }
    return 0;
}

static int32_t
afr_parallel_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
    int call_count = 0;

    afr_common_lock_cbk(frame, cookie, this, op_ret, op_errno, xdata);

    call_count = afr_frame_return(frame);
    if (call_count == 0)
        afr_fop_lock_done(frame, this);

    return 0;
}

static int
afr_parallel_lock_wind(call_frame_t *frame, xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int call_count = 0;
    int i = 0;

    priv = this->private;
    local = frame->local;
    call_count = local->call_count;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->child_up[i])
            continue;
        afr_fop_lock_wind(frame, this, i, afr_parallel_lock_cbk);
        if (!--call_count)
            break;
    }
    return 0;
}

static int
afr_fop_handle_lock(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = frame->local;
    int op_errno = 0;

    if (!afr_fop_lock_is_unlock(frame)) {
        if (!afr_is_consistent_io_possible(local, this->private, &op_errno))
            goto out;

        switch (local->op) {
            case GF_FOP_INODELK:
            case GF_FOP_FINODELK:
                local->cont.inodelk.cmd = F_SETLK;
                break;
            case GF_FOP_ENTRYLK:
            case GF_FOP_FENTRYLK:
                local->cont.entrylk.cmd = ENTRYLK_LOCK_NB;
                break;
            default:
                break;
        }
    }

    if (local->xdata_req) {
        switch (local->op) {
            case GF_FOP_INODELK:
            case GF_FOP_FINODELK:
                local->cont.inodelk.xdata = dict_ref(local->xdata_req);
                break;
            case GF_FOP_ENTRYLK:
            case GF_FOP_FENTRYLK:
                local->cont.entrylk.xdata = dict_ref(local->xdata_req);
                break;
            default:
                break;
        }
    }

    local->fop_lock_state = AFR_FOP_LOCK_PARALLEL;
    afr_parallel_lock_wind(frame, this);
out:
    return -op_errno;
}

static int32_t
afr_handle_inodelk(call_frame_t *frame, xlator_t *this, glusterfs_fop_t fop,
                   const char *volume, loc_t *loc, fd_t *fd, int32_t cmd,
                   struct gf_flock *flock, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = ENOMEM;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = fop;
    if (loc)
        loc_copy(&local->loc, loc);
    if (fd && (flock->l_type != F_UNLCK)) {
        AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
        local->fd = fd_ref(fd);
    }

    local->cont.inodelk.volume = gf_strdup(volume);
    if (!local->cont.inodelk.volume) {
        op_errno = ENOMEM;
        goto out;
    }

    local->cont.inodelk.in_cmd = cmd;
    local->cont.inodelk.cmd = cmd;
    local->cont.inodelk.in_flock = *flock;
    local->cont.inodelk.flock = *flock;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    op_errno = -afr_fop_handle_lock(frame, frame->this);
    if (op_errno)
        goto out;
    return 0;
out:
    afr_fop_lock_unwind(frame, fop, -1, op_errno, NULL);

    return 0;
}

int32_t
afr_inodelk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    afr_handle_inodelk(frame, this, GF_FOP_INODELK, volume, loc, NULL, cmd,
                       flock, xdata);
    return 0;
}

int32_t
afr_finodelk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    afr_handle_inodelk(frame, this, GF_FOP_FINODELK, volume, NULL, fd, cmd,
                       flock, xdata);
    return 0;
}

static int
afr_handle_entrylk(call_frame_t *frame, xlator_t *this, glusterfs_fop_t fop,
                   const char *volume, loc_t *loc, fd_t *fd,
                   const char *basename, entrylk_cmd cmd, entrylk_type type,
                   dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = ENOMEM;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = fop;
    if (loc)
        loc_copy(&local->loc, loc);
    if (fd && (cmd != ENTRYLK_UNLOCK)) {
        AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
        local->fd = fd_ref(fd);
    }
    local->cont.entrylk.cmd = cmd;
    local->cont.entrylk.in_cmd = cmd;
    local->cont.entrylk.type = type;
    local->cont.entrylk.volume = gf_strdup(volume);
    local->cont.entrylk.basename = gf_strdup(basename);
    if (!local->cont.entrylk.volume || !local->cont.entrylk.basename) {
        op_errno = ENOMEM;
        goto out;
    }
    if (xdata)
        local->xdata_req = dict_ref(xdata);
    op_errno = -afr_fop_handle_lock(frame, frame->this);
    if (op_errno)
        goto out;

    return 0;
out:
    afr_fop_lock_unwind(frame, fop, -1, op_errno, NULL);
    return 0;
}

int
afr_entrylk(call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata)
{
    afr_handle_entrylk(frame, this, GF_FOP_ENTRYLK, volume, loc, NULL, basename,
                       cmd, type, xdata);
    return 0;
}

int
afr_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
    afr_handle_entrylk(frame, this, GF_FOP_FENTRYLK, volume, NULL, fd, basename,
                       cmd, type, xdata);
    return 0;
}

int
afr_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct statvfs *statvfs, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = 0;
    struct statvfs *buf = NULL;

    local = frame->local;

    LOCK(&frame->lock);
    {
        if (op_ret != 0) {
            local->op_errno = op_errno;
            goto unlock;
        }

        local->op_ret = op_ret;

        buf = &local->cont.statfs.buf;
        if (local->cont.statfs.buf_set) {
            if (statvfs->f_bavail < buf->f_bavail) {
                *buf = *statvfs;
                if (xdata) {
                    if (local->xdata_rsp)
                        dict_unref(local->xdata_rsp);
                    local->xdata_rsp = dict_ref(xdata);
                }
            }
        } else {
            *buf = *statvfs;
            local->cont.statfs.buf_set = 1;
            if (xdata)
                local->xdata_rsp = dict_ref(xdata);
        }
    }
unlock:
    call_count = --local->call_count;
    UNLOCK(&frame->lock);

    if (call_count == 0)
        AFR_STACK_UNWIND(statfs, frame, local->op_ret, local->op_errno,
                         &local->cont.statfs.buf, local->xdata_rsp);

    return 0;
}

int
afr_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int call_count = 0;
    int32_t op_errno = ENOMEM;

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_STATFS;
    if (!afr_is_consistent_io_possible(local, priv, &op_errno))
        goto out;

    if (priv->arbiter_count == 1 && local->child_up[ARBITER_BRICK_INDEX])
        local->call_count--;
    call_count = local->call_count;
    if (!call_count) {
        op_errno = ENOTCONN;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            if (AFR_IS_ARBITER_BRICK(priv, i))
                continue;
            STACK_WIND(frame, afr_statfs_cbk, priv->children[i],
                       priv->children[i]->fops->statfs, loc, xdata);
            if (!--call_count)
                break;
        }
    }

    return 0;
out:
    AFR_STACK_UNWIND(statfs, frame, -1, op_errno, NULL, NULL);

    return 0;
}

int32_t
afr_lk_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                  dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = this->private;
    int call_count = -1;
    int child_index = (long)cookie;

    local = frame->local;

    if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_UNLOCK_FAIL,
               "gfid=%s: unlock failed on subvolume %s "
               "with lock owner %s",
               uuid_utoa(local->fd->inode->gfid),
               priv->children[child_index]->name,
               lkowner_utoa(&frame->root->lk_owner));
    }

    call_count = afr_frame_return(frame);
    if (call_count == 0) {
        AFR_STACK_UNWIND(lk, frame, local->op_ret, local->op_errno, NULL,
                         local->xdata_rsp);
    }

    return 0;
}

int32_t
afr_lk_unlock(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int call_count = 0;

    local = frame->local;
    priv = this->private;

    call_count = afr_locked_nodes_count(local->cont.lk.locked_nodes,
                                        priv->child_count);

    if (call_count == 0) {
        AFR_STACK_UNWIND(lk, frame, local->op_ret, local->op_errno, NULL,
                         local->xdata_rsp);
        return 0;
    }

    local->call_count = call_count;

    local->cont.lk.user_flock.l_type = F_UNLCK;

    for (i = 0; i < priv->child_count; i++) {
        if (local->cont.lk.locked_nodes[i]) {
            STACK_WIND_COOKIE(frame, afr_lk_unlock_cbk, (void *)(long)i,
                              priv->children[i], priv->children[i]->fops->lk,
                              local->fd, F_SETLK, &local->cont.lk.user_flock,
                              NULL);

            if (!--call_count)
                break;
        }
    }

    return 0;
}

int32_t
afr_lk_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
           int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int child_index = -1;

    local = frame->local;
    priv = this->private;

    child_index = (long)cookie;

    afr_common_lock_cbk(frame, cookie, this, op_ret, op_errno, xdata);
    if (op_ret < 0 && op_errno == EAGAIN) {
        local->op_ret = -1;
        local->op_errno = EAGAIN;

        afr_lk_unlock(frame, this);
        return 0;
    }

    if (op_ret == 0) {
        local->op_ret = 0;
        local->op_errno = 0;
        local->cont.lk.locked_nodes[child_index] = 1;
        local->cont.lk.ret_flock = *lock;
    }

    child_index++;

    if (child_index < priv->child_count) {
        STACK_WIND_COOKIE(frame, afr_lk_cbk, (void *)(long)child_index,
                          priv->children[child_index],
                          priv->children[child_index]->fops->lk, local->fd,
                          local->cont.lk.cmd, &local->cont.lk.user_flock,
                          local->xdata_req);
    } else if (priv->quorum_count &&
               !afr_has_quorum(local->cont.lk.locked_nodes, this, NULL)) {
        local->op_ret = -1;
        local->op_errno = afr_final_errno(local, priv);

        afr_lk_unlock(frame, this);
    } else {
        if (local->op_ret < 0)
            local->op_errno = afr_final_errno(local, priv);

        AFR_STACK_UNWIND(lk, frame, local->op_ret, local->op_errno,
                         &local->cont.lk.ret_flock, local->xdata_rsp);
    }

    return 0;
}

int
afr_lk_transaction_cbk(int ret, call_frame_t *frame, void *opaque)
{
    return 0;
}

int
afr_lk_txn_wind_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                    dict_t *xdata)
{
    afr_local_t *local = NULL;
    int child_index = -1;

    local = frame->local;
    child_index = (long)cookie;
    afr_common_lock_cbk(frame, cookie, this, op_ret, op_errno, xdata);
    if (op_ret == 0) {
        local->op_ret = 0;
        local->op_errno = 0;
        local->cont.lk.locked_nodes[child_index] = 1;
        local->cont.lk.ret_flock = *lock;
    }
    syncbarrier_wake(&local->barrier);
    return 0;
}

int
afr_lk_txn_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                      dict_t *xdata)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = this->private;
    int child_index = (long)cookie;

    if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_UNLOCK_FAIL,
               "gfid=%s: unlock failed on subvolume %s "
               "with lock owner %s",
               uuid_utoa(local->fd->inode->gfid),
               priv->children[child_index]->name,
               lkowner_utoa(&frame->root->lk_owner));
    }
    return 0;
}
int
afr_lk_transaction(void *opaque)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    char *wind_on = NULL;
    int op_errno = 0;
    int i = 0;
    int ret = 0;

    frame = (call_frame_t *)opaque;
    local = frame->local;
    this = frame->this;
    priv = this->private;
    wind_on = alloca0(priv->child_count);

    if (priv->arbiter_count || priv->child_count != 3) {
        op_errno = ENOTSUP;
        gf_msg(frame->this->name, GF_LOG_ERROR, op_errno, AFR_MSG_LK_HEAL_DOM,
               "%s: Lock healing supported only for replica 3 volumes.",
               uuid_utoa(local->fd->inode->gfid));
        goto err;
    }

    op_errno = -afr_dom_lock_acquire(frame);  // Released during
                                              // AFR_STACK_UNWIND
    if (op_errno != 0) {
        goto err;
    }
    if (priv->quorum_count &&
        !afr_has_quorum(local->cont.lk.dom_locked_nodes, this, NULL)) {
        op_errno = afr_final_errno(local, priv);
        goto err;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (priv->child_up[i] && local->cont.lk.dom_locked_nodes[i])
            wind_on[i] = 1;
    }
    AFR_ONLIST(wind_on, frame, afr_lk_txn_wind_cbk, lk, local->fd,
               local->cont.lk.cmd, &local->cont.lk.user_flock,
               local->xdata_req);

    if (priv->quorum_count &&
        !afr_has_quorum(local->cont.lk.locked_nodes, this, NULL)) {
        local->op_ret = -1;
        local->op_errno = afr_final_errno(local, priv);
        goto unlock;
    } else {
        if (local->cont.lk.user_flock.l_type == F_UNLCK)
            ret = afr_remove_lock_from_saved_locks(local, this);
        else
            ret = afr_add_lock_to_saved_locks(frame, this);
        if (ret) {
            local->op_ret = -1;
            local->op_errno = -ret;
            goto unlock;
        }
        AFR_STACK_UNWIND(lk, frame, local->op_ret, local->op_errno,
                         &local->cont.lk.ret_flock, local->xdata_rsp);
    }

    return 0;

unlock:
    local->cont.lk.user_flock.l_type = F_UNLCK;
    AFR_ONLIST(local->cont.lk.locked_nodes, frame, afr_lk_txn_unlock_cbk, lk,
               local->fd, F_SETLK, &local->cont.lk.user_flock, NULL);
err:
    AFR_STACK_UNWIND(lk, frame, -1, op_errno, NULL, NULL);
    return -1;
}

int
afr_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct gf_flock *flock, dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int ret = 0;
    int i = 0;
    int32_t op_errno = ENOMEM;

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_LK;
    if (!afr_lk_is_unlock(cmd, flock)) {
        AFR_ERROR_OUT_IF_FDCTX_INVALID(fd, this, op_errno, out);
        if (!afr_is_consistent_io_possible(local, priv, &op_errno))
            goto out;
    }

    local->cont.lk.locked_nodes = GF_CALLOC(
        priv->child_count, sizeof(*local->cont.lk.locked_nodes),
        gf_afr_mt_char);

    if (!local->cont.lk.locked_nodes) {
        op_errno = ENOMEM;
        goto out;
    }

    local->fd = fd_ref(fd);
    local->cont.lk.cmd = cmd;
    local->cont.lk.user_flock = *flock;
    local->cont.lk.ret_flock = *flock;
    if (xdata) {
        local->xdata_req = dict_ref(xdata);
        if (afr_is_lock_mode_mandatory(xdata)) {
            ret = synctask_new(this->ctx->env, afr_lk_transaction,
                               afr_lk_transaction_cbk, frame, frame);
            if (ret) {
                op_errno = ENOMEM;
                goto out;
            }
            return 0;
        }
    }

    STACK_WIND_COOKIE(frame, afr_lk_cbk, (void *)(long)0, priv->children[i],
                      priv->children[i]->fops->lk, fd, cmd, flock,
                      local->xdata_req);

    return 0;
out:
    AFR_STACK_UNWIND(lk, frame, -1, op_errno, NULL, NULL);

    return 0;
}

int32_t
afr_lease_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct gf_lease *lease,
                     dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = -1;

    local = frame->local;
    call_count = afr_frame_return(frame);

    if (call_count == 0)
        AFR_STACK_UNWIND(lease, frame, local->op_ret, local->op_errno, lease,
                         xdata);

    return 0;
}

int32_t
afr_lease_unlock(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int call_count = 0;

    local = frame->local;
    priv = this->private;

    call_count = afr_locked_nodes_count(local->cont.lease.locked_nodes,
                                        priv->child_count);

    if (call_count == 0) {
        AFR_STACK_UNWIND(lease, frame, local->op_ret, local->op_errno,
                         &local->cont.lease.ret_lease, NULL);
        return 0;
    }

    local->call_count = call_count;

    local->cont.lease.user_lease.cmd = GF_UNLK_LEASE;

    for (i = 0; i < priv->child_count; i++) {
        if (local->cont.lease.locked_nodes[i]) {
            STACK_WIND(frame, afr_lease_unlock_cbk, priv->children[i],
                       priv->children[i]->fops->lease, &local->loc,
                       &local->cont.lease.user_lease, NULL);

            if (!--call_count)
                break;
        }
    }

    return 0;
}

int32_t
afr_lease_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct gf_lease *lease, dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int child_index = -1;

    local = frame->local;
    priv = this->private;

    child_index = (long)cookie;

    afr_common_lock_cbk(frame, cookie, this, op_ret, op_errno, xdata);
    if (op_ret < 0 && op_errno == EAGAIN) {
        local->op_ret = -1;
        local->op_errno = EAGAIN;

        afr_lease_unlock(frame, this);
        return 0;
    }

    if (op_ret == 0) {
        local->op_ret = 0;
        local->op_errno = 0;
        local->cont.lease.locked_nodes[child_index] = 1;
        local->cont.lease.ret_lease = *lease;
    }

    child_index++;
    if (child_index < priv->child_count) {
        STACK_WIND_COOKIE(frame, afr_lease_cbk, (void *)(long)child_index,
                          priv->children[child_index],
                          priv->children[child_index]->fops->lease, &local->loc,
                          &local->cont.lease.user_lease, xdata);
    } else if (priv->quorum_count &&
               !afr_has_quorum(local->cont.lease.locked_nodes, this, NULL)) {
        local->op_ret = -1;
        local->op_errno = afr_final_errno(local, priv);

        afr_lease_unlock(frame, this);
    } else {
        if (local->op_ret < 0)
            local->op_errno = afr_final_errno(local, priv);
        AFR_STACK_UNWIND(lease, frame, local->op_ret, local->op_errno,
                         &local->cont.lease.ret_lease, NULL);
    }

    return 0;
}

int
afr_lease(call_frame_t *frame, xlator_t *this, loc_t *loc,
          struct gf_lease *lease, dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int32_t op_errno = ENOMEM;

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_LEASE;
    local->cont.lease.locked_nodes = GF_CALLOC(
        priv->child_count, sizeof(*local->cont.lease.locked_nodes),
        gf_afr_mt_char);

    if (!local->cont.lease.locked_nodes) {
        op_errno = ENOMEM;
        goto out;
    }

    loc_copy(&local->loc, loc);
    local->cont.lease.user_lease = *lease;
    local->cont.lease.ret_lease = *lease;

    STACK_WIND_COOKIE(frame, afr_lease_cbk, (void *)(long)0, priv->children[0],
                      priv->children[0]->fops->lease, loc, lease, xdata);

    return 0;
out:
    AFR_STACK_UNWIND(lease, frame, -1, op_errno, NULL, NULL);

    return 0;
}

int
afr_ipc_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int child_index = (long)cookie;
    int call_count = 0;
    gf_boolean_t failed = _gf_false;
    gf_boolean_t succeeded = _gf_false;
    int i = 0;
    afr_private_t *priv = NULL;

    local = frame->local;
    priv = this->private;

    local->replies[child_index].valid = 1;
    local->replies[child_index].op_ret = op_ret;
    local->replies[child_index].op_errno = op_errno;
    if (xdata)
        local->replies[child_index].xdata = dict_ref(xdata);

    call_count = afr_frame_return(frame);
    if (call_count)
        goto out;
    /* If any of the subvolumes failed with other than ENOTCONN
     * return error else return success unless all the subvolumes
     * failed.
     * TODO: In case of failure, we need to unregister the xattrs
     * from the other subvolumes where it succeeded (once upcall
     * fixes the Bz-1371622)*/
    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid)
            continue;
        if (local->replies[i].op_ret < 0 &&
            local->replies[i].op_errno != ENOTCONN) {
            local->op_ret = local->replies[i].op_ret;
            local->op_errno = local->replies[i].op_errno;
            if (local->xdata_rsp)
                dict_unref(local->xdata_rsp);
            local->xdata_rsp = NULL;
            if (local->replies[i].xdata) {
                local->xdata_rsp = dict_ref(local->replies[i].xdata);
            }
            failed = _gf_true;
            break;
        }
        if (local->replies[i].op_ret == 0) {
            succeeded = _gf_true;
            local->op_ret = 0;
            local->op_errno = 0;
            if (!local->xdata_rsp && local->replies[i].xdata) {
                local->xdata_rsp = dict_ref(local->replies[i].xdata);
            }
        }
    }

    if (!succeeded && !failed) {
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
    }

    AFR_STACK_UNWIND(ipc, frame, local->op_ret, local->op_errno,
                     local->xdata_rsp);

out:
    return 0;
}

int
afr_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int32_t op_errno = -1;
    afr_private_t *priv = NULL;
    int i = 0;
    int call_cnt = -1;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);

    if (op != GF_IPC_TARGET_UPCALL)
        goto wind_default;

    VALIDATE_OR_GOTO(this->private, err);
    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto err;

    call_cnt = local->call_count;

    if (xdata) {
        for (i = 0; i < priv->child_count; i++) {
            if (dict_set_int8(xdata, priv->pending_key[i], 0) < 0)
                goto err;
        }
    }

    for (i = 0; i < priv->child_count; i++) {
        if (!local->child_up[i])
            continue;

        STACK_WIND_COOKIE(frame, afr_ipc_cbk, (void *)(long)i,
                          priv->children[i], priv->children[i]->fops->ipc, op,
                          xdata);
        if (!--call_cnt)
            break;
    }
    return 0;

err:
    if (op_errno == -1)
        op_errno = errno;
    AFR_STACK_UNWIND(ipc, frame, -1, op_errno, NULL);

    return 0;

wind_default:
    STACK_WIND(frame, default_ipc_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ipc, op, xdata);
    return 0;
}

int
afr_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_int = 0;
    afr_inode_ctx_t *ctx = NULL;

    afr_spb_choice_timeout_cancel(this, inode);
    inode_ctx_del(inode, this, &ctx_int);
    if (!ctx_int)
        return 0;

    ctx = (afr_inode_ctx_t *)(uintptr_t)ctx_int;
    afr_inode_ctx_destroy(ctx);
    return 0;
}

int
afr_priv_dump(xlator_t *this)
{
    afr_private_t *priv = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];
    char key[GF_DUMP_MAX_BUF_LEN];
    int i = 0;

    GF_ASSERT(this);
    priv = this->private;

    GF_ASSERT(priv);
    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section("%s", key_prefix);
    gf_proc_dump_write("child_count", "%u", priv->child_count);
    for (i = 0; i < priv->child_count; i++) {
        sprintf(key, "child_up[%d]", i);
        gf_proc_dump_write(key, "%d", priv->child_up[i]);
        sprintf(key, "pending_key[%d]", i);
        gf_proc_dump_write(key, "%s", priv->pending_key[i]);
        sprintf(key, "pending_reads[%d]", i);
        gf_proc_dump_write(key, "%" PRId64,
                           GF_ATOMIC_GET(priv->pending_reads[i]));
        sprintf(key, "child_latency[%d]", i);
        gf_proc_dump_write(key, "%" PRId64, priv->child_latency[i]);
        sprintf(key, "halo_child_up[%d]", i);
        gf_proc_dump_write(key, "%d", priv->halo_child_up[i]);
    }
    gf_proc_dump_write("data_self_heal", "%d", priv->data_self_heal);
    gf_proc_dump_write("metadata_self_heal", "%d", priv->metadata_self_heal);
    gf_proc_dump_write("entry_self_heal", "%d", priv->entry_self_heal);
    gf_proc_dump_write("read_child", "%d", priv->read_child);
    gf_proc_dump_write("wait_count", "%u", priv->wait_count);
    gf_proc_dump_write("heal-wait-queue-length", "%d", priv->heal_wait_qlen);
    gf_proc_dump_write("heal-waiters", "%d", priv->heal_waiters);
    gf_proc_dump_write("background-self-heal-count", "%d",
                       priv->background_self_heal_count);
    gf_proc_dump_write("healers", "%d", priv->healers);
    gf_proc_dump_write("read-hash-mode", "%d", priv->hash_mode);
    gf_proc_dump_write("use-anonymous-inode", "%d", priv->use_anon_inode);
    if (priv->quorum_count == AFR_QUORUM_AUTO) {
        gf_proc_dump_write("quorum-type", "auto");
    } else if (priv->quorum_count == 0) {
        gf_proc_dump_write("quorum-type", "none");
    } else {
        gf_proc_dump_write("quorum-type", "fixed");
        gf_proc_dump_write("quorum-count", "%d", priv->quorum_count);
    }
    gf_proc_dump_write("up", "%u", afr_has_quorum(priv->child_up, this, NULL));
    if (priv->thin_arbiter_count) {
        gf_proc_dump_write("ta_child_up", "%d", priv->ta_child_up);
        gf_proc_dump_write("ta_bad_child_index", "%d",
                           priv->ta_bad_child_index);
        gf_proc_dump_write("ta_notify_dom_lock_offset", "%" PRId64,
                           priv->ta_notify_dom_lock_offset);
    }

    return 0;
}

/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
afr_find_child_index(xlator_t *this, xlator_t *child)
{
    afr_private_t *priv = NULL;
    int child_count = -1;
    int i = -1;

    priv = this->private;
    child_count = priv->child_count;
    if (priv->thin_arbiter_count) {
        child_count++;
    }

    for (i = 0; i < child_count; i++) {
        if ((xlator_t *)child == priv->children[i])
            break;
    }

    return i;
}

int
__afr_get_up_children_count(afr_private_t *priv)
{
    int up_children = 0;
    int i = 0;

    for (i = 0; i < priv->child_count; i++)
        if (priv->child_up[i] == 1)
            up_children++;

    return up_children;
}

static int
__get_heard_from_all_status(xlator_t *this)
{
    afr_private_t *priv = this->private;
    int i;

    for (i = 0; i < priv->child_count; i++) {
        if (!priv->last_event[i]) {
            return 0;
        }
    }
    if (priv->thin_arbiter_count && !priv->ta_child_up) {
        return 0;
    }
    return 1;
}

glusterfs_event_t
__afr_transform_event_from_state(xlator_t *this)
{
    int i = 0;
    int up_children = 0;
    afr_private_t *priv = this->private;

    if (__get_heard_from_all_status(this))
        /* have_heard_from_all. Let afr_notify() do the propagation. */
        return GF_EVENT_MAXVAL;

    up_children = __afr_get_up_children_count(priv);
    /* Treat the children with pending notification, as having sent a
     * GF_EVENT_CHILD_DOWN. i.e. set the event as GF_EVENT_SOME_DESCENDENT_DOWN,
     * as done in afr_notify() */
    for (i = 0; i < priv->child_count; i++) {
        if (priv->last_event[i])
            continue;
        priv->last_event[i] = GF_EVENT_SOME_DESCENDENT_DOWN;
        priv->child_up[i] = 0;
    }

    if (up_children)
        /* We received at least one child up */
        return GF_EVENT_CHILD_UP;
    else
        return GF_EVENT_CHILD_DOWN;

    return GF_EVENT_MAXVAL;
}

static void
afr_notify_cbk(void *data)
{
    xlator_t *this = data;
    afr_private_t *priv = this->private;
    glusterfs_event_t event = GF_EVENT_MAXVAL;
    gf_boolean_t propagate = _gf_false;

    LOCK(&priv->lock);
    {
        if (!priv->timer) {
            /*
             * Either child_up/child_down is already sent to parent.
             * This is a spurious wake up.
             */
            goto unlock;
        }
        priv->timer = NULL;
        event = __afr_transform_event_from_state(this);
        if (event != GF_EVENT_MAXVAL)
            propagate = _gf_true;
    }
unlock:
    UNLOCK(&priv->lock);
    if (propagate)
        default_notify(this, event, NULL);
}

static void
__afr_launch_notify_timer(xlator_t *this, afr_private_t *priv)
{
    struct timespec delay = {
        0,
    };

    gf_msg_debug(this->name, 0, "Initiating child-down timer");
    delay.tv_sec = 10;
    delay.tv_nsec = 0;
    priv->timer = gf_timer_call_after(this->ctx, delay, afr_notify_cbk, this);
    if (priv->timer == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_TIMER_CREATE_FAIL,
               "Cannot create timer for delayed initialization");
    }
}

static int
find_best_down_child(xlator_t *this)
{
    afr_private_t *priv = NULL;
    int i = -1;
    int32_t best_child = -1;
    int64_t best_latency = INT64_MAX;

    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (!priv->child_up[i] && priv->child_latency[i] >= 0 &&
            priv->child_latency[i] < best_latency) {
            best_child = i;
            best_latency = priv->child_latency[i];
        }
    }
    if (best_child >= 0) {
        gf_msg_debug(this->name, 0,
                     "Found best down child (%d) @ %" PRId64 " ms latency",
                     best_child, best_latency);
    }
    return best_child;
}

int
find_worst_up_child(xlator_t *this)
{
    afr_private_t *priv = NULL;
    int i = -1;
    int32_t worst_child = -1;
    int64_t worst_latency = INT64_MIN;

    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (priv->child_up[i] && priv->child_latency[i] >= 0 &&
            priv->child_latency[i] > worst_latency) {
            worst_child = i;
            worst_latency = priv->child_latency[i];
        }
    }
    if (worst_child >= 0) {
        gf_msg_debug(this->name, 0,
                     "Found worst up child (%d) @ %" PRId64 " ms latency",
                     worst_child, worst_latency);
    }
    return worst_child;
}

void
__afr_handle_ping_event(xlator_t *this, xlator_t *child_xlator, const int idx,
                        int64_t halo_max_latency_msec, int32_t *event,
                        int64_t child_latency_msec)
{
    afr_private_t *priv = NULL;
    int up_children = 0;

    priv = this->private;

    priv->child_latency[idx] = child_latency_msec;
    gf_msg_debug(child_xlator->name, 0, "Client ping @ %" PRId64 " ms",
                 child_latency_msec);
    if (priv->shd.iamshd)
        return;

    up_children = __afr_get_up_children_count(priv);

    if (child_latency_msec > halo_max_latency_msec &&
        priv->child_up[idx] == 1 && up_children > priv->halo_min_replicas) {
        if ((up_children - 1) < priv->halo_min_replicas) {
            gf_log(child_xlator->name, GF_LOG_INFO,
                   "Overriding halo threshold, "
                   "min replicas: %d",
                   priv->halo_min_replicas);
        } else {
            gf_log(child_xlator->name, GF_LOG_INFO,
                   "Child latency (%" PRId64
                   " ms) "
                   "exceeds halo threshold (%" PRId64
                   "), "
                   "marking child down.",
                   child_latency_msec, halo_max_latency_msec);
            if (priv->halo_child_up[idx]) {
                *event = GF_EVENT_CHILD_DOWN;
            }
        }
    } else if (child_latency_msec < halo_max_latency_msec &&
               priv->child_up[idx] == 0) {
        if (up_children < priv->halo_max_replicas) {
            gf_log(child_xlator->name, GF_LOG_INFO,
                   "Child latency (%" PRId64
                   " ms) "
                   "below halo threshold (%" PRId64
                   "), "
                   "marking child up.",
                   child_latency_msec, halo_max_latency_msec);
            if (priv->halo_child_up[idx]) {
                *event = GF_EVENT_CHILD_UP;
            }
        } else {
            gf_log(child_xlator->name, GF_LOG_INFO,
                   "Not marking child %d up, "
                   "max replicas (%d) reached.",
                   idx, priv->halo_max_replicas);
        }
    }
}

static int64_t
afr_get_halo_latency(xlator_t *this)
{
    afr_private_t *priv = NULL;
    int64_t halo_max_latency_msec = 0;

    priv = this->private;

    if (priv->shd.iamshd) {
        halo_max_latency_msec = priv->shd.halo_max_latency_msec;
    } else if (priv->nfsd.iamnfsd) {
        halo_max_latency_msec = priv->nfsd.halo_max_latency_msec;
    } else {
        halo_max_latency_msec = priv->halo_max_latency_msec;
    }
    gf_msg_debug(this->name, 0, "Using halo latency %" PRId64,
                 halo_max_latency_msec);
    return halo_max_latency_msec;
}

void
__afr_handle_child_up_event(xlator_t *this, xlator_t *child_xlator,
                            const int idx, int64_t child_latency_msec,
                            int32_t *event, int32_t *call_psh,
                            int32_t *up_child)
{
    afr_private_t *priv = NULL;
    int up_children = 0;
    int worst_up_child = -1;
    int64_t halo_max_latency_msec = afr_get_halo_latency(this);

    priv = this->private;

    /*
     * This only really counts if the child was never up
     * (value = -1) or had been down (value = 0).  See
     * comment at GF_EVENT_CHILD_DOWN for a more detailed
     * explanation.
     */
    if (priv->child_up[idx] != 1) {
        priv->event_generation++;
    }
    priv->child_up[idx] = 1;

    *call_psh = 1;
    *up_child = idx;
    up_children = __afr_get_up_children_count(priv);
    /*
     * If this is an _actual_ CHILD_UP event, we
     * want to set the child_latency to MAX to indicate
     * the child needs ping data to be available before doing child-up
     */
    if (!priv->halo_enabled)
        goto out;

    if (child_latency_msec < 0) {
        /*set to INT64_MAX-1 so that it is found for best_down_child*/
        priv->halo_child_up[idx] = 1;
        if (priv->child_latency[idx] < 0) {
            priv->child_latency[idx] = AFR_HALO_MAX_LATENCY;
        }
    }

    /*
     * Handle the edge case where we exceed
     * halo_min_replicas and we've got a child which is
     * marked up as it was helping to satisfy the
     * halo_min_replicas even though it's latency exceeds
     * halo_max_latency_msec.
     */
    if (up_children > priv->halo_min_replicas) {
        worst_up_child = find_worst_up_child(this);
        if (worst_up_child >= 0 &&
            priv->child_latency[worst_up_child] > halo_max_latency_msec) {
            gf_msg_debug(this->name, 0,
                         "Marking child %d down, "
                         "doesn't meet halo threshold (%" PRId64
                         "), and > "
                         "halo_min_replicas (%d)",
                         worst_up_child, halo_max_latency_msec,
                         priv->halo_min_replicas);
            priv->child_up[worst_up_child] = 0;
            up_children--;
        }
    }

    if (up_children > priv->halo_max_replicas && !priv->shd.iamshd) {
        worst_up_child = find_worst_up_child(this);
        if (worst_up_child < 0) {
            worst_up_child = idx;
        }
        priv->child_up[worst_up_child] = 0;
        up_children--;
        gf_msg_debug(this->name, 0,
                     "Marking child %d down, "
                     "up_children (%d) > halo_max_replicas (%d)",
                     worst_up_child, up_children, priv->halo_max_replicas);
    }
out:
    if (up_children == 1) {
        gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_SUBVOL_UP,
               "Subvolume '%s' came back up; "
               "going online.",
               child_xlator->name);
        gf_event(EVENT_AFR_SUBVOL_UP, "client-pid=%d; subvol=%s",
                 this->ctx->cmd_args.client_pid, this->name);
    } else {
        *event = GF_EVENT_SOME_DESCENDENT_UP;
    }

    priv->last_event[idx] = *event;
}

void
__afr_handle_child_down_event(xlator_t *this, xlator_t *child_xlator, int idx,
                              int64_t child_latency_msec, int32_t *event,
                              int32_t *call_psh, int32_t *up_child)
{
    afr_private_t *priv = NULL;
    int i = 0;
    int up_children = 0;
    int down_children = 0;
    int best_down_child = -1;

    priv = this->private;

    /*
     * If a brick is down when we start, we'll get a
     * CHILD_DOWN to indicate its initial state.  There
     * was never a CHILD_UP in this case, so if we
     * increment "down_count" the difference between than
     * and "up_count" will no longer be the number of
     * children that are currently up.  This has serious
     * implications e.g. for quorum enforcement, so we
     * don't increment these values unless the event
     * represents an actual state transition between "up"
     * (value = 1) and anything else.
     */
    if (priv->child_up[idx] == 1) {
        priv->event_generation++;
    }

    /*
     * If this is an _actual_ CHILD_DOWN event, we
     * want to set the child_latency to < 0 to indicate
     * the child is really disconnected.
     */
    if (child_latency_msec < 0) {
        priv->child_latency[idx] = child_latency_msec;
        priv->halo_child_up[idx] = 0;
    }
    priv->child_up[idx] = 0;

    up_children = __afr_get_up_children_count(priv);
    /*
     * Handle the edge case where we need to find the
     * next best child (to mark up) as marking this child
     * down would cause us to fall below halo_min_replicas.
     * We will also force the SHD to heal this child _now_
     * as we want it to be up to date if we are going to
     * begin using it synchronously.
     */
    if (priv->halo_enabled && up_children < priv->halo_min_replicas) {
        best_down_child = find_best_down_child(this);
        if (best_down_child >= 0) {
            gf_msg_debug(this->name, 0,
                         "Swapping out child %d for "
                         "child %d to satisfy halo_min_replicas (%d).",
                         idx, best_down_child, priv->halo_min_replicas);
            priv->child_up[best_down_child] = 1;
            *call_psh = 1;
            *up_child = best_down_child;
        }
    }
    for (i = 0; i < priv->child_count; i++)
        if (priv->child_up[i] == 0)
            down_children++;
    if (down_children == priv->child_count) {
        gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_SUBVOLS_DOWN,
               "All subvolumes are down. Going "
               "offline until at least one of them "
               "comes back up.");
        gf_event(EVENT_AFR_SUBVOLS_DOWN, "client-pid=%d; subvol=%s",
                 this->ctx->cmd_args.client_pid, this->name);
    } else {
        *event = GF_EVENT_SOME_DESCENDENT_DOWN;
    }
    priv->last_event[idx] = *event;
}

void
afr_ta_lock_release_synctask(xlator_t *this)
{
    call_frame_t *ta_frame = NULL;
    int ret = 0;

    ta_frame = afr_ta_frame_create(this);
    if (!ta_frame) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to create ta_frame");
        return;
    }

    ret = synctask_new(this->ctx->env, afr_release_notify_lock_for_ta,
                       afr_ta_lock_release_done, ta_frame, this);
    if (ret) {
        STACK_DESTROY(ta_frame->root);
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_THIN_ARB,
               "Failed to release "
               "AFR_TA_DOM_NOTIFY lock.");
    }
}

static void
afr_handle_inodelk_contention(xlator_t *this, struct gf_upcall *upcall)
{
    struct gf_upcall_inodelk_contention *lc = NULL;
    unsigned int inmem_count = 0;
    unsigned int onwire_count = 0;
    afr_private_t *priv = this->private;

    lc = upcall->data;

    if (strcmp(lc->domain, AFR_TA_DOM_NOTIFY) != 0)
        return;

    if (priv->shd.iamshd) {
        /* shd should ignore AFR_TA_DOM_NOTIFY release requests. */
        return;
    }
    LOCK(&priv->lock);
    {
        if (priv->release_ta_notify_dom_lock == _gf_true) {
            /* Ignore multiple release requests from shds.*/
            UNLOCK(&priv->lock);
            return;
        }
        priv->release_ta_notify_dom_lock = _gf_true;
        inmem_count = priv->ta_in_mem_txn_count;
        onwire_count = priv->ta_on_wire_txn_count;
    }
    UNLOCK(&priv->lock);
    if (inmem_count || onwire_count)
        /* lock release will happen in txn code path after
         * in-memory or on-wire txns are over.*/
        return;

    afr_ta_lock_release_synctask(this);
}

static void
afr_handle_upcall_event(xlator_t *this, struct gf_upcall *upcall)
{
    struct gf_upcall_cache_invalidation *up_ci = NULL;
    afr_private_t *priv = this->private;
    inode_t *inode = NULL;
    inode_table_t *itable = NULL;
    int i = 0;

    switch (upcall->event_type) {
        case GF_UPCALL_INODELK_CONTENTION:
            afr_handle_inodelk_contention(this, upcall);
            break;
        case GF_UPCALL_CACHE_INVALIDATION:
            up_ci = (struct gf_upcall_cache_invalidation *)upcall->data;

            /* Since md-cache will be aggressively filtering
             * lookups, the stale read issue will be more
             * pronounced. Hence when a pending xattr is set notify
             * all the md-cache clients to invalidate the existing
             * stat cache and send the lookup next time */
            if (!up_ci->dict)
                break;
            for (i = 0; i < priv->child_count; i++) {
                if (!dict_get(up_ci->dict, priv->pending_key[i]))
                    continue;
                up_ci->flags |= UP_INVAL_ATTR;
                itable = ((xlator_t *)this->graph->top)->itable;
                /*Internal processes may not have itable for
                 *top xlator*/
                if (itable)
                    inode = inode_find(itable, upcall->gfid);
                if (inode)
                    afr_inode_need_refresh_set(inode, this);
                break;
            }
            break;
        default:
            break;
    }
}

int32_t
afr_notify(xlator_t *this, int32_t event, void *data, void *data2)
{
    afr_private_t *priv = NULL;
    xlator_t *child_xlator = NULL;
    int i = -1;
    int propagate = 0;
    int had_heard_from_all = 0;
    int have_heard_from_all = 0;
    int idx = -1;
    int ret = -1;
    int call_psh = 0;
    int up_child = -1;
    dict_t *input = NULL;
    dict_t *output = NULL;
    gf_boolean_t had_quorum = _gf_false;
    gf_boolean_t has_quorum = _gf_false;
    int64_t halo_max_latency_msec = 0;
    int64_t child_latency_msec = -1;

    child_xlator = (xlator_t *)data;

    priv = this->private;

    if (!priv)
        return 0;

    /*
     * We need to reset this in case children come up in "staggered"
     * fashion, so that we discover a late-arriving local subvolume.  Note
     * that we could end up issuing N lookups to the first subvolume, and
     * O(N^2) overall, but N is small for AFR so it shouldn't be an issue.
     */
    priv->did_discovery = _gf_false;

    /* parent xlators don't need to know about every child_up, child_down
     * because of afr ha. If all subvolumes go down, child_down has
     * to be triggered. In that state when 1 subvolume comes up child_up
     * needs to be triggered. dht optimizes revalidate lookup by sending
     * it only to one of its subvolumes. When child up/down happens
     * for afr's subvolumes dht should be notified by child_modified. The
     * subsequent revalidate lookup happens on all the dht's subvolumes
     * which triggers afr self-heals if any.
     */
    idx = afr_find_child_index(this, child_xlator);
    if (idx < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, AFR_MSG_INVALID_CHILD_UP,
               "Received child_up from invalid subvolume");
        goto out;
    }

    had_quorum = priv->quorum_count &&
                 afr_has_quorum(priv->child_up, this, NULL);
    if (event == GF_EVENT_CHILD_PING) {
        child_latency_msec = (int64_t)(uintptr_t)data2;
        if (priv->halo_enabled) {
            halo_max_latency_msec = afr_get_halo_latency(this);

            /* Calculates the child latency and sets event
             */
            LOCK(&priv->lock);
            {
                __afr_handle_ping_event(this, child_xlator, idx,
                                        halo_max_latency_msec, &event,
                                        child_latency_msec);
            }
            UNLOCK(&priv->lock);
        } else {
            LOCK(&priv->lock);
            {
                priv->child_latency[idx] = child_latency_msec;
            }
            UNLOCK(&priv->lock);
        }
    }

    if (event == GF_EVENT_CHILD_PING) {
        /* This is the only xlator that handles PING, no reason to
         * propagate.
         */
        goto out;
    }

    if (event == GF_EVENT_TRANSLATOR_OP) {
        LOCK(&priv->lock);
        {
            had_heard_from_all = __get_heard_from_all_status(this);
        }
        UNLOCK(&priv->lock);

        if (!had_heard_from_all) {
            ret = -1;
        } else {
            input = data;
            output = data2;
            ret = afr_xl_op(this, input, output);
        }
        goto out;
    }

    if (event == GF_EVENT_UPCALL) {
        afr_handle_upcall_event(this, data);
    }

    LOCK(&priv->lock);
    {
        had_heard_from_all = __get_heard_from_all_status(this);
        switch (event) {
            case GF_EVENT_PARENT_UP:
                __afr_launch_notify_timer(this, priv);
                propagate = 1;
                break;
            case GF_EVENT_CHILD_UP:
                if (priv->thin_arbiter_count &&
                    (idx == AFR_CHILD_THIN_ARBITER)) {
                    priv->ta_child_up = 1;
                    priv->ta_event_gen++;
                    break;
                }
                __afr_handle_child_up_event(this, child_xlator, idx,
                                            child_latency_msec, &event,
                                            &call_psh, &up_child);
                __afr_lock_heal_synctask(this, priv, idx);
                break;

            case GF_EVENT_CHILD_DOWN:
                if (priv->thin_arbiter_count &&
                    (idx == AFR_CHILD_THIN_ARBITER)) {
                    priv->ta_child_up = 0;
                    priv->ta_event_gen++;
                    afr_ta_locked_priv_invalidate(priv);
                    break;
                }
                __afr_handle_child_down_event(this, child_xlator, idx,
                                              child_latency_msec, &event,
                                              &call_psh, &up_child);
                __afr_mark_pending_lk_heal(this, priv, idx);
                break;

            case GF_EVENT_CHILD_CONNECTING:
                priv->last_event[idx] = event;

                break;

            case GF_EVENT_SOME_DESCENDENT_DOWN:
                priv->last_event[idx] = event;
                break;
            default:
                propagate = 1;
                break;
        }
        have_heard_from_all = __get_heard_from_all_status(this);
        if (!had_heard_from_all && have_heard_from_all) {
            if (priv->timer) {
                gf_timer_call_cancel(this->ctx, priv->timer);
                priv->timer = NULL;
            }
            /* This is the first event which completes aggregation
               of events from all subvolumes. If at least one subvol
               had come up, propagate CHILD_UP, but only this time
            */
            event = GF_EVENT_CHILD_DOWN;
            for (i = 0; i < priv->child_count; i++) {
                if (priv->last_event[i] == GF_EVENT_CHILD_UP) {
                    event = GF_EVENT_CHILD_UP;
                    break;
                }

                if (priv->last_event[i] == GF_EVENT_CHILD_CONNECTING) {
                    event = GF_EVENT_CHILD_CONNECTING;
                    /* continue to check other events for CHILD_UP */
                }
            }
        }
    }
    UNLOCK(&priv->lock);

    if (priv->quorum_count) {
        has_quorum = afr_has_quorum(priv->child_up, this, NULL);
        if (!had_quorum && has_quorum) {
            gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_QUORUM_MET,
                   "Client-quorum is met");
            gf_event(EVENT_AFR_QUORUM_MET, "client-pid=%d; subvol=%s",
                     this->ctx->cmd_args.client_pid, this->name);
        }
        if (had_quorum && !has_quorum) {
            gf_msg(this->name, GF_LOG_WARNING, 0, AFR_MSG_QUORUM_FAIL,
                   "Client-quorum is not met");
            gf_event(EVENT_AFR_QUORUM_FAIL, "client-pid=%d; subvol=%s",
                     this->ctx->cmd_args.client_pid, this->name);
        }
    }

    /* if all subvols have reported status, no need to hide anything
       or wait for anything else. Just propagate blindly */
    if (have_heard_from_all)
        propagate = 1;

    ret = 0;
    if (propagate)
        ret = default_notify(this, event, data);

    if ((!had_heard_from_all) || call_psh) {
        /* Launch self-heal on all local subvolumes if:
         * a) We have_heard_from_all for the first time
         * b) Already heard from everyone, but we now got a child-up
         *    event.
         */
        if (have_heard_from_all) {
            afr_selfheal_childup(this, priv);
        }
    }
out:
    return ret;
}

int
afr_local_init(afr_local_t *local, afr_private_t *priv, int32_t *op_errno)
{
    int __ret = -1;
    local->op_ret = -1;
    local->op_errno = EUCLEAN;

    __ret = syncbarrier_init(&local->barrier);
    if (__ret) {
        if (op_errno)
            *op_errno = __ret;
        goto out;
    }

    local->child_up = GF_MALLOC(priv->child_count * sizeof(*local->child_up),
                                gf_afr_mt_char);
    if (!local->child_up) {
        if (op_errno)
            *op_errno = ENOMEM;
        goto out;
    }

    memcpy(local->child_up, priv->child_up,
           sizeof(*local->child_up) * priv->child_count);
    local->call_count = AFR_COUNT(local->child_up, priv->child_count);
    if (local->call_count == 0) {
        gf_msg(THIS->name, GF_LOG_INFO, 0, AFR_MSG_SUBVOLS_DOWN,
               "no subvolumes up");
        if (op_errno)
            *op_errno = ENOTCONN;
        goto out;
    }

    local->event_generation = priv->event_generation;

    local->read_attempted = GF_CALLOC(priv->child_count, sizeof(char),
                                      gf_afr_mt_char);
    if (!local->read_attempted) {
        if (op_errno)
            *op_errno = ENOMEM;
        goto out;
    }

    local->readable = GF_CALLOC(priv->child_count, sizeof(char),
                                gf_afr_mt_char);
    if (!local->readable) {
        if (op_errno)
            *op_errno = ENOMEM;
        goto out;
    }

    local->readable2 = GF_CALLOC(priv->child_count, sizeof(char),
                                 gf_afr_mt_char);
    if (!local->readable2) {
        if (op_errno)
            *op_errno = ENOMEM;
        goto out;
    }

    local->read_subvol = -1;

    local->replies = GF_CALLOC(priv->child_count, sizeof(*local->replies),
                               gf_afr_mt_reply_t);
    if (!local->replies) {
        if (op_errno)
            *op_errno = ENOMEM;
        goto out;
    }

    local->need_full_crawl = _gf_false;
    if (priv->thin_arbiter_count) {
        local->ta_child_up = priv->ta_child_up;
        local->ta_failed_subvol = AFR_CHILD_UNKNOWN;
        local->read_txn_query_child = AFR_CHILD_UNKNOWN;
        local->ta_event_gen = priv->ta_event_gen;
        local->fop_state = TA_SUCCESS;
    }
    local->is_new_entry = _gf_false;

    INIT_LIST_HEAD(&local->healer);
    return 0;
out:
    return -1;
}

int
afr_internal_lock_init(afr_internal_lock_t *lk, size_t child_count)
{
    int ret = -ENOMEM;

    lk->lower_locked_nodes = GF_CALLOC(sizeof(*lk->lower_locked_nodes),
                                       child_count, gf_afr_mt_char);
    if (NULL == lk->lower_locked_nodes)
        goto out;

    lk->lock_op_ret = -1;
    lk->lock_op_errno = EUCLEAN;

    ret = 0;
out:
    return ret;
}

void
afr_matrix_cleanup(int32_t **matrix, unsigned int m)
{
    int i = 0;

    if (!matrix)
        goto out;
    for (i = 0; i < m; i++) {
        GF_FREE(matrix[i]);
    }

    GF_FREE(matrix);
out:
    return;
}

int32_t **
afr_matrix_create(unsigned int m, unsigned int n)
{
    int32_t **matrix = NULL;
    int i = 0;

    matrix = GF_CALLOC(sizeof(*matrix), m, gf_afr_mt_int32_t);
    if (!matrix)
        goto out;

    for (i = 0; i < m; i++) {
        matrix[i] = GF_CALLOC(sizeof(*matrix[i]), n, gf_afr_mt_int32_t);
        if (!matrix[i])
            goto out;
    }
    return matrix;
out:
    afr_matrix_cleanup(matrix, m);
    return NULL;
}

int
afr_transaction_local_init(afr_local_t *local, xlator_t *this)
{
    int ret = -ENOMEM;
    afr_private_t *priv = NULL;

    priv = this->private;
    INIT_LIST_HEAD(&local->transaction.wait_list);
    INIT_LIST_HEAD(&local->transaction.owner_list);
    INIT_LIST_HEAD(&local->ta_waitq);
    INIT_LIST_HEAD(&local->ta_onwireq);
    ret = afr_internal_lock_init(&local->internal_lock, priv->child_count);
    if (ret < 0)
        goto out;

    ret = -ENOMEM;
    local->pre_op_compat = priv->pre_op_compat;

    local->transaction.pre_op = GF_CALLOC(sizeof(*local->transaction.pre_op),
                                          priv->child_count, gf_afr_mt_char);
    if (!local->transaction.pre_op)
        goto out;

    local->transaction.changelog_xdata = GF_CALLOC(
        sizeof(*local->transaction.changelog_xdata), priv->child_count,
        gf_afr_mt_dict_t);
    if (!local->transaction.changelog_xdata)
        goto out;

    if (priv->arbiter_count == 1) {
        local->transaction.pre_op_sources = GF_CALLOC(
            sizeof(*local->transaction.pre_op_sources), priv->child_count,
            gf_afr_mt_char);
        if (!local->transaction.pre_op_sources)
            goto out;
    }

    local->transaction.failed_subvols = GF_CALLOC(
        sizeof(*local->transaction.failed_subvols), priv->child_count,
        gf_afr_mt_char);
    if (!local->transaction.failed_subvols)
        goto out;

    local->pending = afr_matrix_create(priv->child_count, AFR_NUM_CHANGE_LOGS);
    if (!local->pending)
        goto out;

    ret = 0;
out:
    return ret;
}

void
afr_set_low_priority(call_frame_t *frame)
{
    frame->root->pid = LOW_PRIO_PROC_PID;
}

void
afr_priv_destroy(afr_private_t *priv)
{
    int i = 0;
    int child_count = -1;

    if (!priv)
        goto out;

    GF_FREE(priv->sh_domain);
    GF_FREE(priv->last_event);

    child_count = priv->child_count;
    if (priv->thin_arbiter_count) {
        child_count++;
    }
    if (priv->pending_key) {
        for (i = 0; i < child_count; i++)
            GF_FREE(priv->pending_key[i]);
    }

    GF_FREE(priv->pending_reads);
    GF_FREE(priv->local);
    GF_FREE(priv->pending_key);
    GF_FREE(priv->children);
    GF_FREE(priv->anon_inode);
    GF_FREE(priv->child_up);
    GF_FREE(priv->halo_child_up);
    GF_FREE(priv->child_latency);
    LOCK_DESTROY(&priv->lock);

    GF_FREE(priv);
out:
    return;
}

int **
afr_mark_pending_changelog(afr_private_t *priv, unsigned char *pending,
                           dict_t *xattr, ia_type_t iat)
{
    int i = 0;
    int **changelog = NULL;
    int idx = -1;
    int m_idx = 0;
    int d_idx = 0;
    int ret = 0;

    m_idx = afr_index_for_transaction_type(AFR_METADATA_TRANSACTION);
    d_idx = afr_index_for_transaction_type(AFR_DATA_TRANSACTION);

    idx = afr_index_from_ia_type(iat);

    changelog = afr_matrix_create(priv->child_count, AFR_NUM_CHANGE_LOGS);
    if (!changelog)
        goto out;

    for (i = 0; i < priv->child_count; i++) {
        if (!pending[i])
            continue;

        changelog[i][m_idx] = hton32(1);
        if (idx != -1)
            changelog[i][idx] = hton32(1);
        /* If the newentry marking is on a newly created directory,
         * then mark it with the full-heal indicator.
         */
        if ((IA_ISDIR(iat)) && (priv->esh_granular))
            changelog[i][d_idx] = hton32(1);
    }
    ret = afr_set_pending_dict(priv, xattr, changelog);
    if (ret < 0) {
        afr_matrix_cleanup(changelog, priv->child_count);
        return NULL;
    }
out:
    return changelog;
}

static dict_t *
afr_set_heal_info(char *status)
{
    dict_t *dict = NULL;
    int ret = -1;

    dict = dict_new();
    if (!dict) {
        ret = -ENOMEM;
        goto out;
    }

    ret = dict_set_dynstr_sizen(dict, "heal-info", status);
    if (ret)
        gf_msg("", GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
               "Failed to set heal-info key to "
               "%s",
               status);
out:
    /* Any error other than EINVAL, dict_set_dynstr frees status */
    if (ret == -ENOMEM || ret == -EINVAL) {
        GF_FREE(status);
    }

    if (ret && dict) {
        dict_unref(dict);
        dict = NULL;
    }
    return dict;
}

static gf_boolean_t
afr_is_dirty_count_non_unary_for_txn(xlator_t *this, struct afr_reply *replies,
                                     afr_transaction_type type)
{
    afr_private_t *priv = this->private;
    int *dirty = alloca0(priv->child_count * sizeof(int));
    int i = 0;

    afr_selfheal_extract_xattr(this, replies, type, dirty, NULL);
    for (i = 0; i < priv->child_count; i++) {
        if (dirty[i] > 1)
            return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
afr_is_dirty_count_non_unary(xlator_t *this, struct afr_reply *replies,
                             ia_type_t ia_type)
{
    gf_boolean_t data_chk = _gf_false;
    gf_boolean_t mdata_chk = _gf_false;
    gf_boolean_t entry_chk = _gf_false;

    switch (ia_type) {
        case IA_IFDIR:
            mdata_chk = _gf_true;
            entry_chk = _gf_true;
            break;
        case IA_IFREG:
            mdata_chk = _gf_true;
            data_chk = _gf_true;
            break;
        default:
            /*IA_IFBLK, IA_IFCHR, IA_IFLNK, IA_IFIFO, IA_IFSOCK*/
            mdata_chk = _gf_true;
            break;
    }

    if (data_chk && afr_is_dirty_count_non_unary_for_txn(
                        this, replies, AFR_DATA_TRANSACTION)) {
        return _gf_true;
    } else if (mdata_chk && afr_is_dirty_count_non_unary_for_txn(
                                this, replies, AFR_METADATA_TRANSACTION)) {
        return _gf_true;
    } else if (entry_chk && afr_is_dirty_count_non_unary_for_txn(
                                this, replies, AFR_ENTRY_TRANSACTION)) {
        return _gf_true;
    }

    return _gf_false;
}

static int
afr_update_heal_status(xlator_t *this, struct afr_reply *replies,
                       ia_type_t ia_type, gf_boolean_t *esh, gf_boolean_t *dsh,
                       gf_boolean_t *msh, unsigned char pending)
{
    int ret = -1;
    GF_UNUSED int ret1 = 0;
    int i = 0;
    int io_domain_lk_count = 0;
    int shd_domain_lk_count = 0;
    afr_private_t *priv = NULL;
    char *key1 = NULL;
    char *key2 = NULL;

    priv = this->private;
    key1 = alloca0(strlen(GLUSTERFS_INODELK_DOM_PREFIX) + 2 +
                   strlen(this->name));
    key2 = alloca0(strlen(GLUSTERFS_INODELK_DOM_PREFIX) + 2 +
                   strlen(priv->sh_domain));
    sprintf(key1, "%s:%s", GLUSTERFS_INODELK_DOM_PREFIX, this->name);
    sprintf(key2, "%s:%s", GLUSTERFS_INODELK_DOM_PREFIX, priv->sh_domain);

    for (i = 0; i < priv->child_count; i++) {
        if ((replies[i].valid != 1) || (replies[i].op_ret != 0))
            continue;
        if (!io_domain_lk_count) {
            ret1 = dict_get_int32(replies[i].xdata, key1, &io_domain_lk_count);
        }
        if (!shd_domain_lk_count) {
            ret1 = dict_get_int32(replies[i].xdata, key2, &shd_domain_lk_count);
        }
    }

    if (!pending) {
        if ((afr_is_dirty_count_non_unary(this, replies, ia_type)) ||
            (!io_domain_lk_count)) {
            /* Needs heal. */
            ret = 0;
        } else {
            /* No heal needed. */
            *dsh = *esh = *msh = 0;
        }
    } else {
        if (shd_domain_lk_count) {
            ret = -EAGAIN; /*For 'possibly-healing'. */
        } else {
            ret = 0; /*needs heal. Just set a non -ve value so that it is
                       assumed as the source index.*/
        }
    }
    return ret;
}

/*return EIO, EAGAIN or pending*/
int
afr_lockless_inspect(call_frame_t *frame, xlator_t *this, uuid_t gfid,
                     inode_t **inode, gf_boolean_t *entry_selfheal,
                     gf_boolean_t *data_selfheal,
                     gf_boolean_t *metadata_selfheal, unsigned char *pending)
{
    int ret = -1;
    int i = 0;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;
    gf_boolean_t dsh = _gf_false;
    gf_boolean_t msh = _gf_false;
    gf_boolean_t esh = _gf_false;
    unsigned char *sources = NULL;
    unsigned char *sinks = NULL;
    unsigned char *valid_on = NULL;
    uint64_t *witness = NULL;

    priv = this->private;
    replies = alloca0(sizeof(*replies) * priv->child_count);
    sources = alloca0(sizeof(*sources) * priv->child_count);
    sinks = alloca0(sizeof(*sinks) * priv->child_count);
    witness = alloca0(sizeof(*witness) * priv->child_count);
    valid_on = alloca0(sizeof(*valid_on) * priv->child_count);

    ret = afr_selfheal_unlocked_inspect(frame, this, gfid, inode, &dsh, &msh,
                                        &esh, replies);
    if (ret)
        goto out;
    for (i = 0; i < priv->child_count; i++) {
        if (replies[i].valid && replies[i].op_ret == 0) {
            valid_on[i] = 1;
        }
    }
    if (msh) {
        ret = afr_selfheal_find_direction(frame, this, replies,
                                          AFR_METADATA_TRANSACTION, valid_on,
                                          sources, sinks, witness, pending);
        if (*pending & PFLAG_SBRAIN)
            ret = -EIO;
        if (ret)
            goto out;
    }
    if (dsh) {
        ret = afr_selfheal_find_direction(frame, this, replies,
                                          AFR_DATA_TRANSACTION, valid_on,
                                          sources, sinks, witness, pending);
        if (*pending & PFLAG_SBRAIN)
            ret = -EIO;
        if (ret)
            goto out;
    }
    if (esh) {
        ret = afr_selfheal_find_direction(frame, this, replies,
                                          AFR_ENTRY_TRANSACTION, valid_on,
                                          sources, sinks, witness, pending);
        if (*pending & PFLAG_SBRAIN)
            ret = -EIO;
        if (ret)
            goto out;
    }

    ret = afr_update_heal_status(this, replies, (*inode)->ia_type, &esh, &dsh,
                                 &msh, *pending);
out:
    *data_selfheal = dsh;
    *entry_selfheal = esh;
    *metadata_selfheal = msh;
    if (replies)
        afr_replies_wipe(replies, priv->child_count);
    return ret;
}

int
afr_get_heal_info(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    gf_boolean_t data_selfheal = _gf_false;
    gf_boolean_t metadata_selfheal = _gf_false;
    gf_boolean_t entry_selfheal = _gf_false;
    unsigned char pending = 0;
    dict_t *dict = NULL;
    int ret = -1;
    int op_errno = ENOMEM;
    inode_t *inode = NULL;
    char *substr = NULL;
    char *status = NULL;
    call_frame_t *heal_frame = NULL;
    afr_local_t *heal_local = NULL;

    /*Use frame with lk-owner set*/
    heal_frame = afr_frame_create(frame->this, &op_errno);
    if (!heal_frame) {
        ret = -1;
        goto out;
    }
    heal_local = heal_frame->local;
    heal_frame->local = frame->local;

    ret = afr_lockless_inspect(heal_frame, this, loc->gfid, &inode,
                               &entry_selfheal, &data_selfheal,
                               &metadata_selfheal, &pending);

    if (ret == -ENOMEM) {
        ret = -1;
        goto out;
    }

    if (pending & PFLAG_PENDING) {
        gf_asprintf(&substr, "-pending");
        if (!substr)
            goto out;
    }

    if (ret == -EIO) {
        ret = gf_asprintf(&status, "split-brain%s", substr ? substr : "");
        if (ret < 0) {
            goto out;
        }
        dict = afr_set_heal_info(status);
        if (!dict) {
            ret = -1;
            goto out;
        }
    } else if (ret == -EAGAIN) {
        ret = gf_asprintf(&status, "possibly-healing%s", substr ? substr : "");
        if (ret < 0) {
            goto out;
        }
        dict = afr_set_heal_info(status);
        if (!dict) {
            ret = -1;
            goto out;
        }
    } else if (ret >= 0) {
        /* value of ret = source index
         * so ret >= 0 and at least one of the 3 booleans set to
         * true means a source is identified; heal is required.
         */
        if (!data_selfheal && !entry_selfheal && !metadata_selfheal) {
            status = gf_strdup("no-heal");
            if (!status) {
                ret = -1;
                goto out;
            }
            dict = afr_set_heal_info(status);
            if (!dict) {
                ret = -1;
                goto out;
            }
        } else {
            ret = gf_asprintf(&status, "heal%s", substr ? substr : "");
            if (ret < 0) {
                goto out;
            }
            dict = afr_set_heal_info(status);
            if (!dict) {
                ret = -1;
                goto out;
            }
        }
    } else if (ret < 0) {
        /* Apart from above checked -ve ret values, there are
         * other possible ret values like ENOTCONN
         * (returned when number of valid replies received are
         * less than 2)
         * in which case heal is required when one of the
         * selfheal booleans is set.
         */
        if (data_selfheal || entry_selfheal || metadata_selfheal) {
            ret = gf_asprintf(&status, "heal%s", substr ? substr : "");
            if (ret < 0) {
                goto out;
            }
            dict = afr_set_heal_info(status);
            if (!dict) {
                ret = -1;
                goto out;
            }
        }
    }

    ret = 0;
    op_errno = 0;

out:
    if (heal_frame) {
        heal_frame->local = heal_local;
        AFR_STACK_DESTROY(heal_frame);
    }
    AFR_STACK_UNWIND(getxattr, frame, ret, op_errno, dict, NULL);
    if (dict)
        dict_unref(dict);
    if (inode)
        inode_unref(inode);
    GF_FREE(substr);
    return ret;
}

int
_afr_is_split_brain(call_frame_t *frame, xlator_t *this,
                    struct afr_reply *replies, afr_transaction_type type,
                    gf_boolean_t *spb)
{
    afr_private_t *priv = NULL;
    uint64_t *witness = NULL;
    unsigned char *sources = NULL;
    unsigned char *sinks = NULL;
    int sources_count = 0;
    int ret = 0;

    priv = this->private;

    sources = alloca0(priv->child_count);
    sinks = alloca0(priv->child_count);
    witness = alloca0(priv->child_count * sizeof(*witness));

    ret = afr_selfheal_find_direction(frame, this, replies, type,
                                      priv->child_up, sources, sinks, witness,
                                      NULL);
    if (ret)
        return ret;

    sources_count = AFR_COUNT(sources, priv->child_count);
    if (!sources_count)
        *spb = _gf_true;

    return ret;
}

int
afr_is_split_brain(call_frame_t *frame, xlator_t *this, inode_t *inode,
                   uuid_t gfid, gf_boolean_t *d_spb, gf_boolean_t *m_spb)
{
    int ret = -1;
    afr_private_t *priv = NULL;
    struct afr_reply *replies = NULL;

    priv = this->private;

    replies = alloca0(sizeof(*replies) * priv->child_count);

    ret = afr_selfheal_unlocked_discover(frame, inode, gfid, replies);
    if (ret)
        goto out;

    if (!afr_can_decide_split_brain_source_sinks(replies, priv->child_count)) {
        ret = -EAGAIN;
        goto out;
    }

    ret = _afr_is_split_brain(frame, this, replies, AFR_DATA_TRANSACTION,
                              d_spb);
    if (ret)
        goto out;

    ret = _afr_is_split_brain(frame, this, replies, AFR_METADATA_TRANSACTION,
                              m_spb);
out:
    if (replies) {
        afr_replies_wipe(replies, priv->child_count);
        replies = NULL;
    }
    return ret;
}

int
afr_get_split_brain_status_cbk(int ret, call_frame_t *frame, void *opaque)
{
    GF_FREE(opaque);
    return 0;
}

int
afr_get_split_brain_status(void *opaque)
{
    gf_boolean_t d_spb = _gf_false;
    gf_boolean_t m_spb = _gf_false;
    int ret = -1;
    int op_errno = 0;
    int i = 0;
    char *choices = NULL;
    char *status = NULL;
    dict_t *dict = NULL;
    inode_t *inode = NULL;
    afr_private_t *priv = NULL;
    xlator_t **children = NULL;
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    loc_t *loc = NULL;
    afr_spb_status_t *data = NULL;

    data = opaque;
    frame = data->frame;
    this = frame->this;
    loc = data->loc;
    priv = this->private;
    children = priv->children;

    inode = afr_inode_find(this, loc->gfid);
    if (!inode)
        goto out;

    dict = dict_new();
    if (!dict) {
        op_errno = ENOMEM;
        ret = -1;
        goto out;
    }

    /* Calculation for string length :
     * (child_count X length of child-name) + SLEN("    Choices :")
     * child-name consists of :
     * a) 251 = max characters for volname according to GD_VOLUME_NAME_MAX
     * b) strlen("-client-00,") assuming 16 replicas
     */
    choices = alloca0(priv->child_count * (256 + SLEN("-client-00,")) +
                      SLEN("    Choices:"));

    ret = afr_is_split_brain(frame, this, inode, loc->gfid, &d_spb, &m_spb);
    if (ret) {
        op_errno = -ret;
        if (ret == -EAGAIN) {
            ret = dict_set_sizen_str_sizen(dict, GF_AFR_SBRAIN_STATUS,
                                           SBRAIN_HEAL_NO_GO_MSG);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, -ret,
                       AFR_MSG_DICT_SET_FAILED,
                       "Failed to set GF_AFR_SBRAIN_STATUS in dict");
            }
        }
        ret = -1;
        goto out;
    }

    if (d_spb || m_spb) {
        sprintf(choices, "    Choices:");
        for (i = 0; i < priv->child_count; i++) {
            strcat(choices, children[i]->name);
            strcat(choices, ",");
        }
        choices[strlen(choices) - 1] = '\0';

        ret = gf_asprintf(&status,
                          "data-split-brain:%s    "
                          "metadata-split-brain:%s%s",
                          (d_spb) ? "yes" : "no", (m_spb) ? "yes" : "no",
                          choices);

        if (-1 == ret) {
            op_errno = ENOMEM;
            goto out;
        }
        ret = dict_set_dynstr_sizen(dict, GF_AFR_SBRAIN_STATUS, status);
        if (ret) {
            op_errno = -ret;
            ret = -1;
            goto out;
        }
    } else {
        ret = dict_set_sizen_str_sizen(dict, GF_AFR_SBRAIN_STATUS,
                                       SFILE_NOT_UNDER_DATA);
        if (ret) {
            op_errno = -ret;
            ret = -1;
            goto out;
        }
    }

    ret = 0;
out:
    AFR_STACK_UNWIND(getxattr, frame, ret, op_errno, dict, NULL);
    if (dict)
        dict_unref(dict);
    if (inode)
        inode_unref(inode);
    return ret;
}

int32_t
afr_heal_splitbrain_file(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    int ret = 0;
    int op_errno = 0;
    dict_t *dict = NULL;
    afr_local_t *local = NULL;
    afr_local_t *heal_local = NULL;
    call_frame_t *heal_frame = NULL;

    local = frame->local;
    dict = dict_new();
    if (!dict) {
        op_errno = ENOMEM;
        ret = -1;
        goto out;
    }

    heal_frame = afr_frame_create(this, &op_errno);
    if (!heal_frame) {
        ret = -1;
        goto out;
    }
    heal_local = heal_frame->local;
    heal_frame->local = frame->local;
    /*Initiate heal with heal_frame with lk-owner set so that inodelk/entrylk
     * work correctly*/
    ret = afr_selfheal_do(heal_frame, this, loc->gfid);

    if (ret == 1 || ret == 2) {
        ret = dict_set_sizen_str_sizen(dict, "sh-fail-msg",
                                       SFILE_NOT_IN_SPLIT_BRAIN);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, -ret, AFR_MSG_DICT_SET_FAILED,
                   "Failed to set sh-fail-msg in dict");
        ret = 0;
        goto out;
    } else {
        if (local->xdata_rsp) {
            /* 'sh-fail-msg' has been set in the dict during self-heal.*/
            dict_copy(local->xdata_rsp, dict);
            ret = 0;
        } else if (ret < 0) {
            op_errno = -ret;
            ret = -1;
        }
    }

out:
    if (heal_frame) {
        heal_frame->local = heal_local;
        AFR_STACK_DESTROY(heal_frame);
    }
    if (local->op == GF_FOP_GETXATTR)
        AFR_STACK_UNWIND(getxattr, frame, ret, op_errno, dict, NULL);
    else if (local->op == GF_FOP_SETXATTR)
        AFR_STACK_UNWIND(setxattr, frame, ret, op_errno, NULL);
    if (dict)
        dict_unref(dict);
    return ret;
}

int
afr_get_child_index_from_name(xlator_t *this, char *name)
{
    afr_private_t *priv = this->private;
    int index = -1;

    for (index = 0; index < priv->child_count; index++) {
        if (!strcmp(priv->children[index]->name, name))
            goto out;
    }
    index = -1;
out:
    return index;
}

void
afr_priv_need_heal_set(afr_private_t *priv, gf_boolean_t need_heal)
{
    LOCK(&priv->lock);
    {
        priv->need_heal = need_heal;
    }
    UNLOCK(&priv->lock);
}

void
afr_set_need_heal(xlator_t *this, afr_local_t *local)
{
    int i = 0;
    afr_private_t *priv = this->private;
    gf_boolean_t need_heal = _gf_false;

    for (i = 0; i < priv->child_count; i++) {
        if (local->replies[i].valid && local->replies[i].need_heal) {
            need_heal = _gf_true;
            break;
        }
    }
    afr_priv_need_heal_set(priv, need_heal);
    return;
}

gf_boolean_t
afr_get_need_heal(xlator_t *this)
{
    afr_private_t *priv = this->private;
    gf_boolean_t need_heal = _gf_true;

    LOCK(&priv->lock);
    {
        need_heal = priv->need_heal;
    }
    UNLOCK(&priv->lock);
    return need_heal;
}

int
afr_get_msg_id(char *op_type)
{
    if (!strcmp(op_type, GF_AFR_REPLACE_BRICK))
        return AFR_MSG_REPLACE_BRICK_STATUS;
    else if (!strcmp(op_type, GF_AFR_ADD_BRICK))
        return AFR_MSG_ADD_BRICK_STATUS;
    return -1;
}

int
afr_fav_child_reset_sink_xattrs_cbk(int ret, call_frame_t *heal_frame,
                                    void *opaque)
{
    call_frame_t *txn_frame = NULL;
    afr_local_t *local = NULL;
    afr_local_t *heal_local = NULL;
    xlator_t *this = NULL;

    heal_local = heal_frame->local;
    txn_frame = heal_local->heal_frame;
    local = txn_frame->local;
    this = txn_frame->this;

    /* Refresh the inode agan and proceed with the transaction.*/
    afr_inode_refresh(txn_frame, this, local->inode, NULL, local->refreshfn);

    AFR_STACK_DESTROY(heal_frame);

    return 0;
}

int
afr_fav_child_reset_sink_xattrs(void *opaque)
{
    call_frame_t *heal_frame = NULL;
    call_frame_t *txn_frame = NULL;
    xlator_t *this = NULL;
    gf_boolean_t d_spb = _gf_false;
    gf_boolean_t m_spb = _gf_false;
    afr_local_t *heal_local = NULL;
    afr_local_t *txn_local = NULL;
    afr_private_t *priv = NULL;
    inode_t *inode = NULL;
    unsigned char *locked_on = NULL;
    unsigned char *sources = NULL;
    unsigned char *sinks = NULL;
    unsigned char *healed_sinks = NULL;
    unsigned char *undid_pending = NULL;
    struct afr_reply *locked_replies = NULL;
    int ret = 0;

    heal_frame = (call_frame_t *)opaque;
    heal_local = heal_frame->local;
    txn_frame = heal_local->heal_frame;
    txn_local = txn_frame->local;
    this = txn_frame->this;
    inode = txn_local->inode;
    priv = this->private;
    locked_on = alloca0(priv->child_count);
    sources = alloca0(priv->child_count);
    sinks = alloca0(priv->child_count);
    healed_sinks = alloca0(priv->child_count);
    undid_pending = alloca0(priv->child_count);
    locked_replies = alloca0(sizeof(*locked_replies) * priv->child_count);

    ret = _afr_is_split_brain(txn_frame, this, txn_local->replies,
                              AFR_DATA_TRANSACTION, &d_spb);

    ret = _afr_is_split_brain(txn_frame, this, txn_local->replies,
                              AFR_METADATA_TRANSACTION, &m_spb);

    /* Take appropriate locks and reset sink xattrs. */
    if (d_spb) {
        ret = afr_selfheal_inodelk(heal_frame, this, inode, this->name, 0, 0,
                                   locked_on);
        {
            if (ret < priv->child_count)
                goto data_unlock;
            ret = __afr_selfheal_data_prepare(
                heal_frame, this, inode, locked_on, sources, sinks,
                healed_sinks, undid_pending, locked_replies, NULL);
        }
    data_unlock:
        afr_selfheal_uninodelk(heal_frame, this, inode, this->name, 0, 0,
                               locked_on);
    }

    if (m_spb) {
        memset(locked_on, 0, sizeof(*locked_on) * priv->child_count);
        memset(undid_pending, 0, sizeof(*undid_pending) * priv->child_count);
        ret = afr_selfheal_inodelk(heal_frame, this, inode, this->name,
                                   LLONG_MAX - 1, 0, locked_on);
        {
            if (ret < priv->child_count)
                goto mdata_unlock;
            ret = __afr_selfheal_metadata_prepare(
                heal_frame, this, inode, locked_on, sources, sinks,
                healed_sinks, undid_pending, locked_replies, NULL);
        }
    mdata_unlock:
        afr_selfheal_uninodelk(heal_frame, this, inode, this->name,
                               LLONG_MAX - 1, 0, locked_on);
    }

    return ret;
}

/*
 * Concatenates the xattrs in local->replies separated by a delimiter.
 */
int
afr_serialize_xattrs_with_delimiter(call_frame_t *frame, xlator_t *this,
                                    char *buf, const char *default_str,
                                    int32_t *serz_len, char delimiter)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    char *xattr = NULL;
    int i = 0;
    int len = 0;
    int keylen = 0;
    size_t str_len = 0;
    int ret = -1;

    priv = this->private;
    local = frame->local;

    keylen = strlen(local->cont.getxattr.name);
    for (i = 0; i < priv->child_count; i++) {
        if (!local->replies[i].valid || local->replies[i].op_ret) {
            str_len = strlen(default_str);
            buf = strncat(buf, default_str, str_len);
            len += str_len;
            buf[len++] = delimiter;
            buf[len] = '\0';
        } else {
            ret = dict_get_strn(local->replies[i].xattr,
                                local->cont.getxattr.name, keylen, &xattr);
            if (ret) {
                gf_msg("TEST", GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
                       "Failed to get the node_uuid of brick "
                       "%d",
                       i);
                goto out;
            }
            str_len = strlen(xattr);
            buf = strncat(buf, xattr, str_len);
            len += str_len;
            buf[len++] = delimiter;
            buf[len] = '\0';
        }
    }
    buf[--len] = '\0'; /*remove the last delimiter*/
    if (serz_len)
        *serz_len = ++len;
    ret = 0;

out:
    return ret;
}

uint64_t
afr_write_subvol_get(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    uint64_t write_subvol = 0;

    local = frame->local;
    LOCK(&local->inode->lock);
    write_subvol = local->inode_ctx->write_subvol;
    UNLOCK(&local->inode->lock);

    return write_subvol;
}

int
afr_write_subvol_set(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    unsigned char *data_accused = NULL;
    unsigned char *metadata_accused = NULL;
    unsigned char *data_readable = NULL;
    unsigned char *metadata_readable = NULL;
    uint16_t datamap = 0;
    uint16_t metadatamap = 0;
    uint64_t val = 0;
    int event = 0;
    int i = 0;

    local = frame->local;
    priv = this->private;
    data_accused = alloca0(priv->child_count);
    metadata_accused = alloca0(priv->child_count);
    data_readable = alloca0(priv->child_count);
    metadata_readable = alloca0(priv->child_count);
    event = local->event_generation;

    afr_readables_fill(frame, this, local->inode, data_accused,
                       metadata_accused, data_readable, metadata_readable,
                       NULL);

    for (i = 0; i < priv->child_count; i++) {
        if (data_readable[i])
            datamap |= (1 << i);
        if (metadata_readable[i])
            metadatamap |= (1 << i);
    }

    val = ((uint64_t)metadatamap) | (((uint64_t)datamap) << 16) |
          (((uint64_t)event) << 32);

    LOCK(&local->inode->lock);
    {
        if (local->inode_ctx->write_subvol == 0 &&
            local->transaction.type == AFR_DATA_TRANSACTION) {
            local->inode_ctx->write_subvol = val;
        }
    }
    UNLOCK(&local->inode->lock);

    return 0;
}

int
afr_write_subvol_reset(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;

    local = frame->local;
    LOCK(&local->inode->lock);
    {
        GF_ASSERT(local->inode_ctx->lock_count > 0);
        local->inode_ctx->lock_count--;

        if (!local->inode_ctx->lock_count)
            local->inode_ctx->write_subvol = 0;
    }
    UNLOCK(&local->inode->lock);

    return 0;
}

int
afr_set_inode_local(xlator_t *this, afr_local_t *local, inode_t *inode)
{
    int ret = 0;

    local->inode = inode_ref(inode);
    LOCK(&local->inode->lock);
    {
        ret = __afr_inode_ctx_get(this, local->inode, &local->inode_ctx);
    }
    UNLOCK(&local->inode->lock);
    if (ret < 0) {
        gf_msg_callingfn(
            this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_INODE_CTX_GET_FAILED,
            "Error getting inode ctx %s", uuid_utoa(local->inode->gfid));
    }
    return ret;
}

gf_boolean_t
afr_ta_is_fop_called_from_synctask(xlator_t *this)
{
    struct synctask *task = NULL;
    gf_lkowner_t tmp_owner = {
        0,
    };

    task = synctask_get();
    if (!task)
        return _gf_false;

    set_lk_owner_from_ptr(&tmp_owner, (void *)this);

    if (!is_same_lkowner(&tmp_owner, &task->frame->root->lk_owner))
        return _gf_false;

    return _gf_true;
}

int
afr_ta_post_op_lock(xlator_t *this, loc_t *loc)
{
    int ret = 0;
    uuid_t gfid = {
        0,
    };
    afr_private_t *priv = this->private;
    gf_boolean_t locked = _gf_false;
    struct gf_flock flock1 = {
        0,
    };
    struct gf_flock flock2 = {
        0,
    };
    int32_t cmd = 0;

    /* Clients must take AFR_TA_DOM_NOTIFY lock only when the previous lock
     * has been released in afr_notify due to upcall notification from shd.
     */
    GF_ASSERT(priv->ta_notify_dom_lock_offset == 0);

    if (!priv->shd.iamshd)
        GF_ASSERT(afr_ta_is_fop_called_from_synctask(this));
    flock1.l_type = F_WRLCK;

    while (!locked) {
        if (priv->shd.iamshd) {
            cmd = F_SETLKW;
            flock1.l_start = 0;
            flock1.l_len = 0;
        } else {
            cmd = F_SETLK;
            gf_uuid_generate(gfid);
            flock1.l_start = gfid_to_ino(gfid);
            if (flock1.l_start < 0)
                flock1.l_start = -flock1.l_start;
            flock1.l_len = 1;
        }
        ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                             AFR_TA_DOM_NOTIFY, loc, cmd, &flock1, NULL, NULL);
        if (!ret) {
            locked = _gf_true;
            priv->ta_notify_dom_lock_offset = flock1.l_start;
        } else if (ret == -EAGAIN) {
            continue;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
                   "Failed to get "
                   "AFR_TA_DOM_NOTIFY lock on %s.",
                   loc->name);
            goto out;
        }
    }

    flock2.l_type = F_WRLCK;
    flock2.l_start = 0;
    flock2.l_len = 0;
    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_MODIFY, loc, F_SETLKW, &flock2, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to get AFR_TA_DOM_MODIFY lock on %s.", loc->name);
        flock1.l_type = F_UNLCK;
        ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                             AFR_TA_DOM_NOTIFY, loc, F_SETLK, &flock1, NULL,
                             NULL);
    }
out:
    return ret;
}

int
afr_ta_post_op_unlock(xlator_t *this, loc_t *loc)
{
    afr_private_t *priv = this->private;
    struct gf_flock flock = {
        0,
    };
    int ret = 0;

    if (!priv->shd.iamshd)
        GF_ASSERT(afr_ta_is_fop_called_from_synctask(this));
    flock.l_type = F_UNLCK;
    flock.l_start = 0;
    flock.l_len = 0;

    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_MODIFY, loc, F_SETLK, &flock, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to unlock AFR_TA_DOM_MODIFY lock.");
        goto out;
    }

    if (!priv->shd.iamshd)
        /* Mounts (clients) will not release the AFR_TA_DOM_NOTIFY lock
         * in post-op as they use it as a notification mechanism. When
         * shd sends a lock request on TA during heal, the clients will
         * receive a lock-contention upcall notification upon which they
         * will release the AFR_TA_DOM_NOTIFY lock after completing the
         * in flight I/O.*/
        goto out;

    ret = syncop_inodelk(priv->children[THIN_ARBITER_BRICK_INDEX],
                         AFR_TA_DOM_NOTIFY, loc, F_SETLK, &flock, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to unlock AFR_TA_DOM_NOTIFY lock.");
    }
out:
    return ret;
}

call_frame_t *
afr_ta_frame_create(xlator_t *this)
{
    call_frame_t *frame = NULL;
    void *lk_owner = NULL;

    frame = create_frame(this, this->ctx->pool);
    if (!frame)
        return NULL;
    lk_owner = (void *)this;
    afr_set_lk_owner(frame, this, lk_owner);
    return frame;
}

gf_boolean_t
afr_ta_has_quorum(afr_private_t *priv, afr_local_t *local)
{
    int data_count = 0;

    data_count = AFR_COUNT(local->child_up, priv->child_count);
    if (data_count == 2) {
        return _gf_true;
    } else if (data_count == 1 && local->ta_child_up) {
        return _gf_true;
    }

    return _gf_false;
}

static gf_boolean_t
afr_is_add_replica_mount_lookup_on_root(call_frame_t *frame)
{
    afr_local_t *local = NULL;

    if (frame->root->pid != GF_CLIENT_PID_ADD_REPLICA_MOUNT)
        return _gf_false;

    local = frame->local;

    if (local->op != GF_FOP_LOOKUP)
        /* TODO:If the replica count is being increased on a plain distribute
         * volume that was never mounted, we need to allow setxattr on '/' with
         * GF_CLIENT_PID_NO_ROOT_SQUASH to accomodate for DHT layout setting */
        return _gf_false;

    if (local->inode == NULL)
        return _gf_false;

    if (!__is_root_gfid(local->inode->gfid))
        return _gf_false;

    return _gf_true;
}

gf_boolean_t
afr_lookup_has_quorum(call_frame_t *frame, const unsigned int up_children_count)
{
    if (frame && (up_children_count > 0) &&
        afr_is_add_replica_mount_lookup_on_root(frame))
        return _gf_true;

    return _gf_false;
}

void
afr_handle_replies_quorum(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = this->private;
    unsigned char *success_replies = NULL;

    success_replies = alloca0(priv->child_count);
    afr_fill_success_replies(local, priv, success_replies);

    if (priv->quorum_count && !afr_has_quorum(success_replies, this, NULL)) {
        local->op_errno = afr_final_errno(local, priv);
        if (!local->op_errno)
            local->op_errno = afr_quorum_errno(priv);
        local->op_ret = -1;
    }
}

gf_boolean_t
afr_ta_dict_contains_pending_xattr(dict_t *dict, afr_private_t *priv, int child)
{
    int *pending = NULL;
    int ret = 0;
    int i = 0;

    ret = dict_get_ptr(dict, priv->pending_key[child], (void *)&pending);
    if (ret == 0) {
        for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++) {
            /* Not doing a ntoh32(pending) as we just want to check
             * if it is non-zero or not. */
            if (pending[i]) {
                return _gf_true;
            }
        }
    }

    return _gf_false;
}
