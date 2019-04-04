/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/dict.h>
#include <glusterfs/byte-order.h>
#include <glusterfs/common-utils.h>

#include "afr.h"
#include "afr-transaction.h"
#include "afr-messages.h"

#include <signal.h>

#define LOCKED_NO 0x0    /* no lock held */
#define LOCKED_YES 0x1   /* for DATA, METADATA, ENTRY and higher_path */
#define LOCKED_LOWER 0x2 /* for lower path */

void
afr_lockee_cleanup(afr_lockee_t *lockee)
{
    if (lockee->fd) {
        fd_unref(lockee->fd);
        lockee->fd = NULL;
    } else {
        loc_wipe(&lockee->loc);
    }

    GF_FREE(lockee->basename);
    lockee->basename = NULL;
    GF_FREE(lockee->locked_nodes);
    lockee->locked_nodes = NULL;

    return;
}

void
afr_lockees_cleanup(afr_internal_lock_t *int_lock)
{
    int i = 0;

    for (i = 0; i < int_lock->lockee_count; i++) {
        afr_lockee_cleanup(&int_lock->lockee[i]);
    }

    return;
}
int
afr_entry_lockee_cmp(const void *l1, const void *l2)
{
    const afr_lockee_t *r1 = l1;
    const afr_lockee_t *r2 = l2;
    int ret = 0;
    uuid_t gfid1 = {0};
    uuid_t gfid2 = {0};

    loc_gfid((loc_t *)&r1->loc, gfid1);
    loc_gfid((loc_t *)&r2->loc, gfid2);
    ret = gf_uuid_compare(gfid1, gfid2);
    /*Entrylks with NULL basename are the 'smallest'*/
    if (ret == 0) {
        if (!r1->basename)
            return -1;
        if (!r2->basename)
            return 1;
        ret = strcmp(r1->basename, r2->basename);
    }

    if (ret <= 0)
        return -1;
    else
        return 1;
}

int
afr_lock_blocking(call_frame_t *frame, xlator_t *this, int child_index);

void
afr_set_lk_owner(call_frame_t *frame, xlator_t *this, void *lk_owner)
{
    gf_msg_trace(this->name, 0, "Setting lk-owner=%llu",
                 (unsigned long long)(unsigned long)lk_owner);

    set_lk_owner_from_ptr(&frame->root->lk_owner, lk_owner);
}

int32_t
internal_lock_count(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int32_t call_count = 0;
    int i = 0;

    local = frame->local;
    priv = this->private;

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i])
            ++call_count;
    }

    return call_count;
}

int
afr_add_entry_lockee(afr_local_t *local, loc_t *loc, char *basename,
                     int child_count)
{
    int ret = -ENOMEM;
    afr_internal_lock_t *int_lock = &local->internal_lock;
    afr_lockee_t *lockee = &int_lock->lockee[int_lock->lockee_count];

    GF_ASSERT(int_lock->lockee_count < AFR_LOCKEE_COUNT_MAX);
    loc_copy(&lockee->loc, loc);
    lockee->basename = (basename) ? gf_strdup(basename) : NULL;
    if (basename && !lockee->basename)
        goto out;

    lockee->locked_count = 0;
    lockee->locked_nodes = GF_CALLOC(child_count, sizeof(*lockee->locked_nodes),
                                     gf_afr_mt_afr_node_character);

    if (!lockee->locked_nodes)
        goto out;

    ret = 0;
    int_lock->lockee_count++;
out:
    if (ret) {
        afr_lockee_cleanup(lockee);
    }
    return ret;
}

int
afr_add_inode_lockee(afr_local_t *local, int child_count)
{
    int ret = -ENOMEM;
    afr_internal_lock_t *int_lock = &local->internal_lock;
    afr_lockee_t *lockee = &int_lock->lockee[int_lock->lockee_count];

    if (local->fd) {
        lockee->fd = fd_ref(local->fd);
    } else {
        loc_copy(&lockee->loc, &local->loc);
    }

    lockee->locked_count = 0;
    lockee->locked_nodes = GF_CALLOC(child_count, sizeof(*lockee->locked_nodes),
                                     gf_afr_mt_afr_node_character);

    if (!lockee->locked_nodes)
        goto out;

    ret = 0;
    int_lock->lockee_count++;
out:
    if (ret) {
        afr_lockee_cleanup(lockee);
    }
    return ret;
}

static int
initialize_internal_lock_variables(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_internal_lock_t *int_lock = NULL;
    afr_private_t *priv = NULL;

    int i = 0;

    priv = this->private;
    local = frame->local;
    int_lock = &local->internal_lock;

    int_lock->lock_count = 0;
    int_lock->lock_op_ret = -1;
    int_lock->lock_op_errno = 0;
    int_lock->lk_attempted_count = 0;

    for (i = 0; i < AFR_LOCKEE_COUNT_MAX; i++) {
        if (!int_lock->lockee[i].locked_nodes)
            break;
        int_lock->lockee[i].locked_count = 0;
        memset(int_lock->lockee[i].locked_nodes, 0,
               sizeof(*int_lock->lockee[i].locked_nodes) * priv->child_count);
    }

    return 0;
}

int
afr_lockee_locked_nodes_count(afr_internal_lock_t *int_lock)
{
    int call_count = 0;
    int i = 0;

    for (i = 0; i < int_lock->lockee_count; i++)
        call_count += int_lock->lockee[i].locked_count;

    return call_count;
}

int
afr_locked_nodes_count(unsigned char *locked_nodes, int child_count)

{
    int i = 0;
    int call_count = 0;

    for (i = 0; i < child_count; i++) {
        if (locked_nodes[i] & LOCKED_YES)
            call_count++;
    }

    return call_count;
}

static void
afr_log_locks_failure(call_frame_t *frame, char *where, char *what,
                      int op_errno)
{
    xlator_t *this = frame->this;
    gf_lkowner_t *lk_owner = &frame->root->lk_owner;
    afr_local_t *local = frame->local;
    const char *fop = NULL;
    char *gfid = NULL;
    const char *name = NULL;

    fop = gf_fop_list[local->op];

    switch (local->transaction.type) {
        case AFR_ENTRY_RENAME_TRANSACTION:
        case AFR_ENTRY_TRANSACTION:
            switch (local->op) {
                case GF_FOP_LINK:
                    gfid = uuid_utoa(local->newloc.pargfid);
                    name = local->newloc.name;
                    break;
                default:
                    gfid = uuid_utoa(local->loc.pargfid);
                    name = local->loc.name;
                    break;
            }
            gf_msg(this->name, GF_LOG_WARNING, op_errno,
                   AFR_MSG_INTERNAL_LKS_FAILED,
                   "Unable to do entry %s with lk-owner:%s on %s "
                   "while attempting %s on {pgfid:%s, name:%s}.",
                   what, lkowner_utoa(lk_owner), where, fop, gfid, name);
            break;
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
            gfid = uuid_utoa(local->inode->gfid);
            gf_msg(this->name, GF_LOG_WARNING, op_errno,
                   AFR_MSG_INTERNAL_LKS_FAILED,
                   "Unable to do inode %s with lk-owner:%s on %s "
                   "while attempting %s on gfid:%s.",
                   what, lkowner_utoa(lk_owner), where, fop, gfid);
            break;
    }
}

static int32_t
afr_unlock_common_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_internal_lock_t *int_lock = NULL;
    int lockee_num = 0;
    int call_count = 0;
    int child_index = 0;
    int ret = 0;

    local = frame->local;
    int_lock = &local->internal_lock;
    priv = this->private;
    lockee_num = (int)((long)cookie) / priv->child_count;
    child_index = (int)((long)cookie) % priv->child_count;

    if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
        afr_log_locks_failure(frame, priv->children[child_index]->name,
                              "unlock", op_errno);
    }

    int_lock->lockee[lockee_num].locked_nodes[child_index] &= LOCKED_NO;
    if (local->transaction.type == AFR_DATA_TRANSACTION && op_ret != 1)
        ret = afr_write_subvol_reset(frame, this);

    LOCK(&frame->lock);
    {
        call_count = --int_lock->lk_call_count;
    }
    UNLOCK(&frame->lock);

    if (call_count == 0) {
        int_lock->lock_cbk(frame, this);
    }

    return ret;
}

void
afr_internal_lock_wind(call_frame_t *frame,
                       int32_t (*cbk)(call_frame_t *, void *, xlator_t *,
                                      int32_t, int32_t, dict_t *),
                       void *cookie, int child, int lockee_num,
                       gf_boolean_t blocking, gf_boolean_t unlock)
{
    afr_local_t *local = frame->local;
    xlator_t *this = frame->this;
    afr_private_t *priv = this->private;
    afr_internal_lock_t *int_lock = &local->internal_lock;
    entrylk_cmd cmd = ENTRYLK_LOCK_NB;
    int32_t cmd1 = F_SETLK;
    struct gf_flock flock = {
        0,
    };

    switch (local->transaction.type) {
        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
            if (unlock) {
                cmd = ENTRYLK_UNLOCK;
            } else if (blocking) { /*Doesn't make sense to have blocking
                                      unlock*/
                cmd = ENTRYLK_LOCK;
            }

            if (local->fd) {
                STACK_WIND_COOKIE(frame, cbk, cookie, priv->children[child],
                                  priv->children[child]->fops->fentrylk,
                                  int_lock->domain,
                                  int_lock->lockee[lockee_num].fd,
                                  int_lock->lockee[lockee_num].basename, cmd,
                                  ENTRYLK_WRLCK, NULL);
            } else {
                STACK_WIND_COOKIE(frame, cbk, cookie, priv->children[child],
                                  priv->children[child]->fops->entrylk,
                                  int_lock->domain,
                                  &int_lock->lockee[lockee_num].loc,
                                  int_lock->lockee[lockee_num].basename, cmd,
                                  ENTRYLK_WRLCK, NULL);
            }
            break;

        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
            flock = int_lock->lockee[lockee_num].flock;
            if (unlock) {
                flock.l_type = F_UNLCK;
            } else if (blocking) { /*Doesn't make sense to have blocking
                                      unlock*/
                cmd1 = F_SETLKW;
            }

            if (local->fd) {
                STACK_WIND_COOKIE(
                    frame, cbk, cookie, priv->children[child],
                    priv->children[child]->fops->finodelk, int_lock->domain,
                    int_lock->lockee[lockee_num].fd, cmd1, &flock, NULL);
            } else {
                STACK_WIND_COOKIE(
                    frame, cbk, cookie, priv->children[child],
                    priv->children[child]->fops->inodelk, int_lock->domain,
                    &int_lock->lockee[lockee_num].loc, cmd1, &flock, NULL);
            }
            break;
    }
}

static int
afr_unlock_now(call_frame_t *frame, xlator_t *this)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = 0;
    int child_index = 0;
    int lockee_num = 0;
    int i = -1;

    local = frame->local;
    int_lock = &local->internal_lock;
    priv = this->private;

    call_count = afr_lockee_locked_nodes_count(int_lock);

    int_lock->lk_call_count = call_count;

    if (!call_count) {
        gf_msg_trace(this->name, 0, "No internal locks unlocked");
        int_lock->lock_cbk(frame, this);
        goto out;
    }

    for (i = 0; i < int_lock->lockee_count * priv->child_count; i++) {
        lockee_num = i / priv->child_count;
        child_index = i % priv->child_count;
        if (int_lock->lockee[lockee_num].locked_nodes[child_index] &
            LOCKED_YES) {
            afr_internal_lock_wind(frame, afr_unlock_common_cbk,
                                   (void *)(long)i, child_index, lockee_num,
                                   _gf_false, _gf_true);
            if (!--call_count)
                break;
        }
    }

out:
    return 0;
}

static int32_t
afr_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, dict_t *xdata)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int cky = (long)cookie;
    int child_index = 0;
    int lockee_num = 0;

    priv = this->private;
    local = frame->local;
    int_lock = &local->internal_lock;

    child_index = ((int)cky) % priv->child_count;
    lockee_num = ((int)cky) / priv->child_count;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            if (op_errno == ENOSYS) {
                /* return ENOTSUP */
                gf_msg(this->name, GF_LOG_ERROR, ENOSYS,
                       AFR_MSG_LOCK_XLATOR_NOT_LOADED,
                       "subvolume does not support locking. "
                       "please load features/locks xlator on server");
                local->op_ret = op_ret;
                int_lock->lock_op_ret = op_ret;
            }

            local->op_errno = op_errno;
            int_lock->lock_op_errno = op_errno;
        }

        int_lock->lk_attempted_count++;
    }
    UNLOCK(&frame->lock);

    if ((op_ret == -1) && (op_errno == ENOSYS)) {
        afr_unlock_now(frame, this);
    } else {
        if (op_ret == 0) {
            int_lock->lockee[lockee_num]
                .locked_nodes[child_index] |= LOCKED_YES;
            int_lock->lockee[lockee_num].locked_count++;
            int_lock->lock_count++;
            if (local->transaction.type == AFR_DATA_TRANSACTION) {
                LOCK(&local->inode->lock);
                {
                    local->inode_ctx->lock_count++;
                }
                UNLOCK(&local->inode->lock);
            }
        }
        afr_lock_blocking(frame, this, cky + 1);
    }

    return 0;
}

static gf_boolean_t
_is_lock_wind_needed(afr_local_t *local, int child_index)
{
    if (!local->child_up[child_index])
        return _gf_false;

    return _gf_true;
}

static gf_boolean_t
is_blocking_locks_count_sufficient(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_internal_lock_t *int_lock = NULL;
    int child = 0;
    int nlockee = 0;
    int lockee_count = 0;
    gf_boolean_t ret = _gf_true;

    local = frame->local;
    priv = this->private;
    int_lock = &local->internal_lock;
    lockee_count = int_lock->lockee_count;

    if (int_lock->lock_count == 0) {
        afr_log_locks_failure(frame, "any subvolume", "lock",
                              int_lock->lock_op_errno);
        return _gf_false;
    }
    /* For FOPS that take multiple sets of locks (mkdir, rename),
     * there must be at least one brick on which the locks from
     * all lock sets were successful. */
    for (child = 0; child < priv->child_count; child++) {
        ret = _gf_true;
        for (nlockee = 0; nlockee < lockee_count; nlockee++) {
            if (!(int_lock->lockee[nlockee].locked_nodes[child] & LOCKED_YES))
                ret = _gf_false;
        }
        if (ret)
            return ret;
    }
    if (!ret)
        afr_log_locks_failure(frame, "all", "lock", int_lock->lock_op_errno);

    return ret;
}

int
afr_lock_blocking(call_frame_t *frame, xlator_t *this, int cookie)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    uint64_t ctx = 0;
    int ret = 0;
    int child_index = 0;
    int lockee_num = 0;

    local = frame->local;
    int_lock = &local->internal_lock;
    priv = this->private;
    child_index = cookie % priv->child_count;
    lockee_num = cookie / priv->child_count;

    if (local->fd) {
        ret = fd_ctx_get(local->fd, this, &ctx);

        if (ret < 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_FD_CTX_GET_FAILED,
                   "unable to get fd ctx for fd=%p", local->fd);

            local->op_ret = -1;
            int_lock->lock_op_ret = -1;

            afr_unlock_now(frame, this);

            return 0;
        }
    }

    if (int_lock->lk_expected_count == int_lock->lk_attempted_count) {
        if (!is_blocking_locks_count_sufficient(frame, this)) {
            local->op_ret = -1;
            int_lock->lock_op_ret = -1;

            afr_unlock_now(frame, this);

            return 0;
        }
    }

    if (int_lock->lk_expected_count == int_lock->lk_attempted_count) {
        /* we're done locking */

        gf_msg_debug(this->name, 0, "we're done locking");

        int_lock->lock_op_ret = 0;
        int_lock->lock_cbk(frame, this);
        return 0;
    }

    if (!_is_lock_wind_needed(local, child_index)) {
        afr_lock_blocking(frame, this, cookie + 1);
        return 0;
    }

    afr_internal_lock_wind(frame, afr_lock_cbk, (void *)(long)cookie,
                           child_index, lockee_num, _gf_true, _gf_false);

    return 0;
}

int32_t
afr_blocking_lock(call_frame_t *frame, xlator_t *this)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int up_count = 0;

    priv = this->private;
    local = frame->local;
    int_lock = &local->internal_lock;

    up_count = AFR_COUNT(local->child_up, priv->child_count);
    int_lock->lk_call_count = int_lock->lk_expected_count =
        (int_lock->lockee_count * up_count);
    initialize_internal_lock_variables(frame, this);

    afr_lock_blocking(frame, this, 0);

    return 0;
}

static int32_t
afr_nb_internal_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    int call_count = 0;
    int child_index = 0;
    int lockee_num = 0;
    afr_private_t *priv = NULL;

    priv = this->private;

    child_index = ((long)cookie) % priv->child_count;
    lockee_num = ((long)cookie) / priv->child_count;

    local = frame->local;
    int_lock = &local->internal_lock;

    if (op_ret == 0 && local->transaction.type == AFR_DATA_TRANSACTION) {
        LOCK(&local->inode->lock);
        {
            local->inode_ctx->lock_count++;
        }
        UNLOCK(&local->inode->lock);
    }

    LOCK(&frame->lock);
    {
        if (op_ret < 0) {
            if (op_errno == ENOSYS) {
                /* return ENOTSUP */
                gf_msg(this->name, GF_LOG_ERROR, ENOSYS,
                       AFR_MSG_LOCK_XLATOR_NOT_LOADED,
                       "subvolume does not support "
                       "locking. please load features/locks"
                       " xlator on server");
                local->op_ret = op_ret;
                int_lock->lock_op_ret = op_ret;

                int_lock->lock_op_errno = op_errno;
                local->op_errno = op_errno;
            }
        } else if (op_ret == 0) {
            int_lock->lockee[lockee_num]
                .locked_nodes[child_index] |= LOCKED_YES;
            int_lock->lockee[lockee_num].locked_count++;
            int_lock->lock_count++;
        }

        call_count = --int_lock->lk_call_count;
    }
    UNLOCK(&frame->lock);

    if (call_count == 0) {
        gf_msg_trace(this->name, 0, "Last locking reply received");
        /* all locks successful. Proceed to call FOP */
        if (int_lock->lock_count == int_lock->lk_expected_count) {
            gf_msg_trace(this->name, 0, "All servers locked. Calling the cbk");
            int_lock->lock_op_ret = 0;
            int_lock->lock_cbk(frame, this);
        }
        /* Not all locks were successful. Unlock and try locking
           again, this time with serially blocking locks */
        else {
            gf_msg_trace(this->name, 0,
                         "%d servers locked. Trying again "
                         "with blocking calls",
                         int_lock->lock_count);

            afr_unlock_now(frame, this);
        }
    }

    return 0;
}

int
afr_lock_nonblocking(call_frame_t *frame, xlator_t *this)
{
    afr_internal_lock_t *int_lock = NULL;
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_fd_ctx_t *fd_ctx = NULL;
    int child = 0;
    int lockee_num = 0;
    int32_t call_count = 0;
    int i = 0;
    int ret = 0;

    local = frame->local;
    int_lock = &local->internal_lock;
    priv = this->private;

    initialize_internal_lock_variables(frame, this);

    if (local->fd) {
        fd_ctx = afr_fd_ctx_get(local->fd, this);
        if (!fd_ctx) {
            gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_FD_CTX_GET_FAILED,
                   "unable to get fd ctx for fd=%p", local->fd);

            local->op_ret = -1;
            int_lock->lock_op_ret = -1;
            local->op_errno = EINVAL;
            int_lock->lock_op_errno = EINVAL;

            afr_unlock_now(frame, this);
            ret = -1;
            goto out;
        }
    }

    call_count = int_lock->lockee_count * internal_lock_count(frame, this);
    int_lock->lk_call_count = call_count;
    int_lock->lk_expected_count = call_count;

    if (!call_count) {
        gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_INFO_COMMON,
               "fd not open on any subvolumes. aborting.");
        afr_unlock_now(frame, this);
        goto out;
    }

    /* Send non-blocking lock calls only on up children
       and where the fd has been opened */
    for (i = 0; i < int_lock->lockee_count * priv->child_count; i++) {
        child = i % priv->child_count;
        lockee_num = i / priv->child_count;
        if (local->child_up[child]) {
            afr_internal_lock_wind(frame, afr_nb_internal_lock_cbk,
                                   (void *)(long)i, child, lockee_num,
                                   _gf_false, _gf_false);
            if (!--call_count)
                break;
        }
    }
out:
    return ret;
}

int32_t
afr_unlock(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = NULL;
    afr_lock_t *lock = NULL;

    local = frame->local;

    if (!local->transaction.eager_lock_on)
        goto out;
    lock = &local->inode_ctx->lock[local->transaction.type];
    LOCK(&local->inode->lock);
    {
        list_del_init(&local->transaction.owner_list);
        if (list_empty(&lock->owners) && list_empty(&lock->post_op)) {
            local->transaction.do_eager_unlock = _gf_true;
            /*TODO: Need to get metadata use on_disk and inherit/uninherit
             *GF_ASSERT (!local->inode_ctx->on_disk[local->transaction.type]);
             *GF_ASSERT (!local->inode_ctx->inherited[local->transaction.type]);
             */
            GF_ASSERT(lock->release);
        }
    }
    UNLOCK(&local->inode->lock);
    if (!local->transaction.do_eager_unlock) {
        local->internal_lock.lock_cbk(frame, this);
        return 0;
    }

out:
    afr_unlock_now(frame, this);
    return 0;
}
