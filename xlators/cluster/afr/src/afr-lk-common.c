/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "dict.h"
#include "byte-order.h"
#include "common-utils.h"

#include "afr.h"
#include "afr-transaction.h"

#include <signal.h>


#define LOCKED_NO       0x0        /* no lock held */
#define LOCKED_YES      0x1        /* for DATA, METADATA, ENTRY and higher_path */
#define LOCKED_LOWER    0x2        /* for lower path */

int
afr_lock_blocking (call_frame_t *frame, xlator_t *this, int child_index);

static uint64_t afr_lock_number = 1;

static uint64_t
get_afr_lock_number ()
{
        return (++afr_lock_number);
}

int
afr_set_lock_number (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->lock_number = get_afr_lock_number ();

        return 0;
}

void
afr_set_lk_owner (call_frame_t *frame, xlator_t *this)
{
        gf_log (this->name, GF_LOG_TRACE,
                "Setting lk-owner=%llu",
                (unsigned long long) (unsigned long)frame->root);
        frame->root->lk_owner = (uint64_t) (unsigned long)frame->root;
}

static int
is_afr_lock_selfheal (afr_local_t *local)
{
        afr_internal_lock_t *int_lock = NULL;
        int                  ret      = -1;

        int_lock = &local->internal_lock;

        switch (int_lock->selfheal_lk_type) {
        case AFR_DATA_SELF_HEAL_LK:
        case AFR_METADATA_SELF_HEAL_LK:
                ret = 1;
                break;
        case AFR_ENTRY_SELF_HEAL_LK:
                ret = 0;
                break;
        }

        return ret;

}

int32_t
internal_lock_count (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int32_t call_count = 0;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        if (local->fd) {
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] && local->fd_open_on[i])
                                ++call_count;
                }
        } else {
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i])
                                ++call_count;
                }
        }

        return call_count;
}

static void
afr_print_inodelk (char *str, int size, int cmd,
                   struct gf_flock *flock, uint64_t owner)
{
        char *cmd_str = NULL;
        char *type_str = NULL;

        switch (cmd) {
#if F_GETLK != F_GETLK64
        case F_GETLK64:
#endif
        case F_GETLK:
                cmd_str = "GETLK";
                break;

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
                cmd_str = "SETLK";
                break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:
                cmd_str = "SETLKW";
                break;

        default:
                cmd_str = "<null>";
                break;
        }

        switch (flock->l_type) {
        case F_RDLCK:
                type_str = "READ";
                break;
        case F_WRLCK:
                type_str = "WRITE";
                break;
        case F_UNLCK:
                type_str = "UNLOCK";
                break;
        default:
                type_str = "UNKNOWN";
                break;
        }

        snprintf (str, size, "lock=INODELK, cmd=%s, type=%s, "
                  "start=%llu, len=%llu, pid=%llu, lk-owner=%llu",
                  cmd_str, type_str, (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid,
                  (unsigned long long) owner);

}

static void
afr_print_lockee (char *str, int size, loc_t *loc, fd_t *fd,
                  int child_index)
{
        snprintf (str, size, "path=%s, fd=%p, child=%d",
                  loc->path ? loc->path : "<nul>",
                  fd ? fd : NULL,
                  child_index);
}

void
afr_print_entrylk (char *str, int size, const char *basename,
                   uint64_t owner)
{
        snprintf (str, size, "Basename=%s, lk-owner=%llu",
                  basename ? basename : "<nul>",
                  (unsigned long long)owner);
}

static void
afr_print_verdict (int op_ret, int op_errno, char *str)
{
        if (op_ret < 0) {
                if (op_errno == EAGAIN)
                        strcpy (str, "EAGAIN");
                else
                        strcpy (str, "FAILED");
        }
        else
                strcpy (str, "GRANTED");
}

static void
afr_set_lock_call_type (afr_lock_call_type_t lock_call_type,
                        char *lock_call_type_str,
                        afr_internal_lock_t *int_lock)
{
        switch (lock_call_type) {
        case AFR_INODELK_TRANSACTION:
                if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK)
                        strcpy (lock_call_type_str, "AFR_INODELK_TRANSACTION");
                else
                        strcpy (lock_call_type_str, "AFR_INODELK_SELFHEAL");
                break;
        case AFR_INODELK_NB_TRANSACTION:
                if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK)
                        strcpy (lock_call_type_str, "AFR_INODELK_NB_TRANSACTION");
                else
                        strcpy (lock_call_type_str, "AFR_INODELK_NB_SELFHEAL");
                break;
        case AFR_ENTRYLK_TRANSACTION:
                if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK)
                        strcpy (lock_call_type_str, "AFR_ENTRYLK_TRANSACTION");
                else
                        strcpy (lock_call_type_str, "AFR_ENTRYLK_SELFHEAL");
                break;
        case AFR_ENTRYLK_NB_TRANSACTION:
                if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK)
                        strcpy (lock_call_type_str, "AFR_ENTRYLK_NB_TRANSACTION");
                else
                        strcpy (lock_call_type_str, "AFR_ENTRYLK_NB_SELFHEAL");
                break;
        default:
                strcpy (lock_call_type_str, "UNKNOWN");
                break;
        }

}

static void
afr_trace_inodelk_out (call_frame_t *frame, afr_lock_call_type_t lock_call_type,
                       afr_lock_op_type_t lk_op_type, struct gf_flock *flock,
                       int op_ret, int op_errno, int32_t child_index)
{
        xlator_t            *this     = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        char lockee[256];
        char lock_call_type_str[256];
        char verdict[16];

        this     = THIS;
        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->inodelk_trace) {
                return;
        }

        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        afr_print_verdict (op_ret, op_errno, verdict);

        gf_log (this->name, GF_LOG_INFO,
                "[%s %s] [%s] Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REPLY" : "UNLOCK REPLY",
                verdict,
                lockee,
                (unsigned long long) int_lock->lock_number);

}

static void
afr_trace_inodelk_in (call_frame_t *frame, afr_lock_call_type_t lock_call_type,
                      afr_lock_op_type_t lk_op_type, struct gf_flock *flock,
                      int32_t cmd, int32_t child_index)
{
        xlator_t            *this     = NULL;
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];

        this     = THIS;
        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->inodelk_trace) {
                return;
        }

        afr_print_inodelk (lock, 256, cmd, flock, frame->root->lk_owner);
        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        gf_log (this->name, GF_LOG_INFO,
                "[%s %s] Lock={%s} Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REQUEST" : "UNLOCK REQUEST",
                lock, lockee,
                (unsigned long long) int_lock->lock_number);

}

static void
afr_trace_entrylk_in (call_frame_t *frame, afr_lock_call_type_t lock_call_type,
                      afr_lock_op_type_t lk_op_type, const char *basename,
                      int32_t child_index)
{
        xlator_t            *this     = NULL;
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];

        this     = THIS;
        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->entrylk_trace) {
                return;
        }

        afr_print_entrylk (lock, 256, basename, frame->root->lk_owner);
        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        gf_log (this->name, GF_LOG_INFO,
                "[%s %s] Lock={%s} Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REQUEST" : "UNLOCK REQUEST",
                lock, lockee,
                (unsigned long long) int_lock->lock_number);
}

static void
afr_trace_entrylk_out (call_frame_t *frame, afr_lock_call_type_t lock_call_type,
                       afr_lock_op_type_t lk_op_type, const char *basename, int op_ret,
                       int op_errno, int32_t child_index)
{
        xlator_t            *this     = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];
        char verdict[16];

        this     = THIS;
        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->entrylk_trace) {
                return;
        }

        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        afr_print_verdict (op_ret, op_errno, verdict);

        gf_log (this->name, GF_LOG_INFO,
                "[%s %s] [%s] Lock={%s} Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REPLY" : "UNLOCK REPLY",
                verdict,
                lock, lockee,
                (unsigned long long) int_lock->lock_number);

}

static int
transaction_lk_op (afr_local_t *local)
{
        afr_internal_lock_t *int_lock = NULL;
        int ret = -1;

        int_lock = &local->internal_lock;

        if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "lk op is for a transaction");
                ret = 1;
        }
        else if (int_lock->transaction_lk_type == AFR_SELFHEAL_LK) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "lk op is for a self heal");

                ret = 0;
        }

        if (ret == -1)
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "lk op is not set");

        return ret;

}

static int
is_afr_lock_transaction (afr_local_t *local)
{
        int ret = 0;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                ret = 1;
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
        case AFR_ENTRY_TRANSACTION:
                ret = 0;
                break;

        }

        return ret;
}

static int
initialize_entrylk_variables (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;

        int i = 0;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->entrylk_lock_count = 0;
        int_lock->lock_op_ret        = -1;
        int_lock->lock_op_errno      = 0;

        for (i = 0; i < priv->child_count; i++) {
                int_lock->entry_locked_nodes[i] = 0;
        }

        return 0;
}

static int
initialize_inodelk_variables (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        int i = 0;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->inodelk_lock_count = 0;
        int_lock->lock_op_ret        = -1;
        int_lock->lock_op_errno      = 0;

        for (i = 0; i < priv->child_count; i++) {
                int_lock->inode_locked_nodes[i] = 0;
        }

        return 0;
}

loc_t *
lower_path (loc_t *l1, const char *b1, loc_t *l2, const char *b2)
{
        int ret = 0;

        ret = strcmp (l1->path, l2->path);

        if (ret == 0)
                ret = strcmp (b1, b2);

        if (ret <= 0)
                return l1;
        else
                return l2;
}

int
afr_locked_nodes_count (unsigned char *locked_nodes, int child_count)

{
        int i = 0;
        int call_count = 0;

        for (i = 0; i < child_count; i++) {
                if (locked_nodes[i] & LOCKED_YES)
                        call_count++;
        }

        return call_count;
}

/* FIXME: What if UNLOCK fails */
static int32_t
afr_unlock_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        int call_count = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                call_count = --int_lock->lk_call_count;
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "All internal locks unlocked");
                int_lock->lock_cbk (frame, this);
        }

        return 0;
}

static int32_t
afr_unlock_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        afr_local_t         *local = NULL;
        afr_internal_lock_t *int_lock = NULL;
        int32_t             child_index = (long)cookie;

        local = frame->local;
        int_lock = &local->internal_lock;

        afr_trace_inodelk_out (frame, AFR_INODELK_TRANSACTION,
                               AFR_UNLOCK_OP, NULL, op_ret,
                               op_errno, child_index);

        if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: unlock failed on %d, reason: %s",
                        local->loc.path, child_index, strerror (op_errno));
        }


        int_lock->inode_locked_nodes[child_index] &= LOCKED_NO;

        if (op_ret == 1) {
                local->transaction.eager_lock[child_index] = 0;
        }

        afr_unlock_common_cbk (frame, cookie, this, op_ret, op_errno);

        return 0;

}

static int
afr_unlock_inodelk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        struct gf_flock flock = {0,};
        int call_count = 0;
        int i = 0;
        int piggyback = 0;
        afr_fd_ctx_t        *fd_ctx      = NULL;


        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        flock.l_start = int_lock->lk_flock.l_start;
        flock.l_len   = int_lock->lk_flock.l_len;
        flock.l_type  = F_UNLCK;

        gf_log (this->name, GF_LOG_DEBUG, "attempting data unlock range %"PRIu64
                " %"PRIu64" by %"PRIu64, flock.l_start, flock.l_len,
                frame->root->lk_owner);

        call_count = afr_locked_nodes_count (int_lock->inode_locked_nodes,
                                             priv->child_count);

        int_lock->lk_call_count = call_count;

        if (!call_count) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No internal locks unlocked");
                int_lock->lock_cbk (frame, this);
                goto out;
        }

        if (local->fd)
                fd_ctx = afr_fd_ctx_get (local->fd, this);

        for (i = 0; i < priv->child_count; i++) {
                if ((int_lock->inode_locked_nodes[i] & LOCKED_YES)
                    != LOCKED_YES)
                        continue;

                if (local->fd) {
                        if (!local->transaction.eager_lock[i]) {
                                goto wind;
                        }

                        piggyback = 0;

                        LOCK (&local->fd->lock);
                        {
                                if (fd_ctx->lock_piggyback[i]) {
                                        fd_ctx->lock_piggyback[i]--;
                                        piggyback = 1;
                                }
                        }
                        UNLOCK (&local->fd->lock);

                        if (piggyback) {
                                afr_unlock_inodelk_cbk (frame, (void *) (long) i,
                                                        this, 1, 0);
                                if (!--call_count)
                                        break;
                                continue;
                        }

                        fd_ctx->lock_acquired[i]--;
                wind:
                        afr_trace_inodelk_in (frame, AFR_INODELK_TRANSACTION,
                                              AFR_UNLOCK_OP, &flock, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_unlock_inodelk_cbk,
                                           (void *) (long)i,
                                           priv->children[i],
                                           priv->children[i]->fops->finodelk,
                                           this->name, local->fd,
                                           F_SETLK, &flock);

                        if (!--call_count)
                                break;

                } else {
                        afr_trace_inodelk_in (frame, AFR_INODELK_TRANSACTION,
                                              AFR_UNLOCK_OP, &flock, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_unlock_inodelk_cbk,
                                           (void *) (long)i,
                                           priv->children[i],
                                           priv->children[i]->fops->inodelk,
                                           this->name, &local->loc,
                                           F_SETLK, &flock);

                        if (!--call_count)
                                break;
                }
        }
out:
        return 0;
}

static int32_t
afr_unlock_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        afr_local_t         *local = NULL;
        int32_t             child_index = (long)cookie;

        local = frame->local;

        afr_trace_entrylk_out (frame, AFR_ENTRYLK_TRANSACTION,
                               AFR_UNLOCK_OP, NULL, op_ret,
                               op_errno, child_index);

        if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: unlock failed on %d, reason: %s",
                        local->loc.path, child_index, strerror (op_errno));
        }

        afr_unlock_common_cbk (frame, cookie, this, op_ret, op_errno);

        return 0;
}

static int
afr_unlock_entrylk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        const char          *basename = NULL;
        loc_t               *loc      = NULL;
        int call_count = 0;
        int i = -1;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        basename = int_lock->lk_basename;
        if (int_lock->lk_loc)
                loc = int_lock->lk_loc;

        call_count = afr_locked_nodes_count (int_lock->entry_locked_nodes,
                                             priv->child_count);
        int_lock->lk_call_count = call_count;

        if (!call_count){
                gf_log (this->name, GF_LOG_TRACE,
                        "No internal locks unlocked");
                int_lock->lock_cbk (frame, this);
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (int_lock->entry_locked_nodes[i] & LOCKED_YES) {
                        afr_trace_entrylk_in (frame, AFR_ENTRYLK_NB_TRANSACTION,
                                              AFR_UNLOCK_OP, basename, i);

                        STACK_WIND_COOKIE (frame, afr_unlock_entrylk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->entrylk,
                                           this->name,
                                           loc, basename,
                                           ENTRYLK_UNLOCK, ENTRYLK_WRLCK);

                        if (!--call_count)
                                break;
                }
        }

out:
        return 0;

}

static int32_t
afr_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        int child_index = (long) cookie;

        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        if (op_errno == ENOSYS) {
                                /* return ENOTSUP */
                                gf_log (this->name, GF_LOG_ERROR,
                                        "subvolume does not support locking. "
                                        "please load features/locks xlator on server");
                                local->op_ret = op_ret;
                                int_lock->lock_op_ret = op_ret;
                        }

                        local->op_errno              = op_errno;
                        int_lock->lock_op_errno      = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if ((op_ret == -1) &&
            (op_errno == ENOSYS)) {
                afr_unlock (frame, this);
        } else {
                if (op_ret == 0) {
                        int_lock->locked_nodes[child_index] |= LOCKED_YES;
                        int_lock->lock_count++;
                }
                afr_lock_blocking (frame, this, child_index + 1);
        }

        return 0;
}

static int32_t
afr_blocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno)
{
        afr_trace_inodelk_out (frame, AFR_INODELK_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

        afr_lock_cbk (frame, cookie, this, op_ret, op_errno);
        return 0;

}

static int32_t
afr_lock_lower_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_private_t       *priv        = NULL;
        afr_local_t         *local       = NULL;
        loc_t               *lower       = NULL;
        loc_t               *higher      = NULL;
        const char          *higher_name = NULL;
        int child_index = (long) cookie;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        if (op_errno == ENOSYS) {
                                /* return ENOTSUP */

                                gf_log (this->name, GF_LOG_ERROR,
                                        "subvolume does not support locking. "
                                        "please load features/locks xlator on server");

                                local->op_ret   = op_ret;
                        }

                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if (op_ret != 0) {
                afr_unlock (frame, this);
                goto out;
        } else {
                int_lock->lower_locked_nodes[child_index] |= LOCKED_LOWER;
                int_lock->lock_count++;
        }

        /* The lower path has been locked. Now lock the higher path */

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        higher = (lower == &local->transaction.parent_loc ?
                  &local->transaction.new_parent_loc :
                  &local->transaction.parent_loc);

        higher_name = (higher == &local->transaction.parent_loc ?
                       local->transaction.basename :
                       local->transaction.new_basename);

        afr_trace_entrylk_in (frame, AFR_ENTRYLK_TRANSACTION,
                              AFR_LOCK_OP, higher_name, child_index);


        STACK_WIND_COOKIE (frame, afr_lock_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->entrylk,
                           this->name, higher, higher_name,
                           ENTRYLK_LOCK, ENTRYLK_WRLCK);

out:
        return 0;
}

static int32_t
afr_blocking_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno)
{
        afr_trace_entrylk_out (frame, AFR_ENTRYLK_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long)cookie);

        afr_lock_cbk (frame, cookie, this, op_ret, op_errno);
        return 0;
}

static int
afr_copy_locked_nodes (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                memcpy (int_lock->inode_locked_nodes,
                        int_lock->locked_nodes,
                        priv->child_count);
                int_lock->inodelk_lock_count = int_lock->lock_count;
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
        case AFR_ENTRY_TRANSACTION:
                memcpy (int_lock->entry_locked_nodes,
                        int_lock->locked_nodes,
                        priv->child_count);
                int_lock->entrylk_lock_count = int_lock->lock_count;
                break;
        }

        return 0;

}

int
afr_lock_blocking (call_frame_t *frame, xlator_t *this, int child_index)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_local_t         *local       = NULL;
        afr_private_t       *priv        = NULL;
        loc_t               *lower       = NULL;
        const char          *lower_name  = NULL;
        struct gf_flock flock = {0,};
        uint64_t ctx = 0;
        int ret = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        flock.l_start = int_lock->lk_flock.l_start;
        flock.l_len   = int_lock->lk_flock.l_len;
        flock.l_type  = int_lock->lk_flock.l_type;

        if (local->fd) {
                ret = fd_ctx_get (local->fd, this, &ctx);

                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;

                        afr_copy_locked_nodes (frame, this);

                        afr_unlock (frame, this);

                        return 0;
                }

                /* skip over children that or down
                   or don't have the fd open */

                while ((child_index < priv->child_count)
                       && (!local->child_up[child_index] ||
                          !local->fd_open_on[child_index]))

                        child_index++;
        } else {
                /* skip over children that are down */
                while ((child_index < priv->child_count)
                       && !local->child_up[child_index])
                        child_index++;
        }

        if ((child_index == priv->child_count) &&
            int_lock->lock_count == 0) {

                gf_log (this->name, GF_LOG_INFO,
                        "unable to lock on even one child");

                local->op_ret           = -1;
                int_lock->lock_op_ret   = -1;

                afr_copy_locked_nodes (frame, this);

                afr_unlock(frame, this);

                return 0;

        }

        if ((child_index == priv->child_count)
            || (int_lock->lock_count == int_lock->lk_expected_count)) {

                /* we're done locking */

                gf_log (this->name, GF_LOG_DEBUG,
                        "we're done locking");

                afr_copy_locked_nodes (frame, this);

                int_lock->lock_op_ret = 0;
                int_lock->lock_cbk (frame, this);
                return 0;
        }

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:

                if (local->fd) {
                        afr_trace_inodelk_in (frame, AFR_INODELK_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLKW,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_inodelk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->finodelk,
                                           this->name, local->fd,
                                           F_SETLKW, &flock);

                } else {
                        afr_trace_inodelk_in (frame, AFR_INODELK_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLKW,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_inodelk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->inodelk,
                                           this->name, &local->loc,
                                           F_SETLKW, &flock);
                }

                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
        {
                lower = lower_path (&local->transaction.parent_loc,
                                    local->transaction.basename,
                                    &local->transaction.new_parent_loc,
                                    local->transaction.new_basename);

                lower_name = (lower == &local->transaction.parent_loc ?
                              local->transaction.basename :
                              local->transaction.new_basename);

                afr_trace_entrylk_in (frame, AFR_ENTRYLK_TRANSACTION,
                                      AFR_LOCK_OP, lower_name, child_index);


                STACK_WIND_COOKIE (frame, afr_lock_lower_cbk,
                                   (void *) (long) child_index,
                                   priv->children[child_index],
                                   priv->children[child_index]->fops->entrylk,
                                   this->name, lower, lower_name,
                                   ENTRYLK_LOCK, ENTRYLK_WRLCK);

                break;
        }

        case AFR_ENTRY_TRANSACTION:
                if (local->fd) {
                        afr_trace_entrylk_in (frame, AFR_ENTRYLK_TRANSACTION,
                                              AFR_LOCK_OP, local->transaction.basename,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_entrylk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->fentrylk,
                                           this->name, local->fd,
                                           local->transaction.basename,
                                           ENTRYLK_LOCK, ENTRYLK_WRLCK);
                } else {
                        afr_trace_entrylk_in (frame, AFR_ENTRYLK_TRANSACTION,
                                              AFR_LOCK_OP, local->transaction.basename,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_entrylk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->entrylk,
                                           this->name,
                                           &local->transaction.parent_loc,
                                           local->transaction.basename,
                                           ENTRYLK_LOCK, ENTRYLK_WRLCK);
                }

                break;
        }

        return 0;
}

int32_t
afr_blocking_lock (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        int                  up_count = 0;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                initialize_inodelk_variables (frame, this);
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
                up_count = afr_up_children_count (local->child_up,
                                                  priv->child_count);
                int_lock->lk_expected_count = 2 * up_count;
                //fallthrough
        case AFR_ENTRY_TRANSACTION:
                initialize_entrylk_variables (frame, this);
                break;
        }

        afr_lock_blocking (frame, this, 0);

        return 0;
}

static int32_t
afr_nonblocking_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        int call_count          = 0;
        int child_index         = (long) cookie;

        local    = frame->local;
        int_lock = &local->internal_lock;

        afr_trace_entrylk_out (frame, AFR_ENTRYLK_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

        LOCK (&frame->lock);
        {
                call_count = --int_lock->lk_call_count;
        }
        UNLOCK (&frame->lock);

        if (op_ret < 0 ) {
                if (op_errno == ENOSYS) {
                        /* return ENOTSUP */
                        gf_log (this->name, GF_LOG_ERROR,
                                "subvolume does not support locking. "
                                "please load features/locks xlator on server");
                        local->op_ret         = op_ret;
                        int_lock->lock_op_ret = op_ret;

                        int_lock->lock_op_errno      = op_errno;
                        local->op_errno              = op_errno;
                }
        } else if (op_ret == 0) {
                int_lock->entry_locked_nodes[child_index] |= LOCKED_YES;
                int_lock->entrylk_lock_count++;
        }

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Last locking reply received");
                /* all locks successful. Proceed to call FOP */
                if (int_lock->entrylk_lock_count ==
                                int_lock->lk_expected_count) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successful. Unlock and try locking
                   again, this time with serially blocking locks */
                else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "%d servers locked. Trying again with blocking calls",
                                int_lock->lock_count);

                        afr_unlock(frame, this);
                }
        }

        return 0;
}

void
afr_mark_fd_open_on (afr_local_t *local, afr_fd_ctx_t *fd_ctx,
                       size_t child_count)
{
        int             i = 0;

        GF_ASSERT (local->fd_open_on);

        memset (local->fd_open_on, 0, sizeof (*local->fd_open_on)*child_count);
        for (i = 0; i < child_count; i++)
                if (fd_ctx->opened_on[i] == AFR_FD_OPENED)
                        local->fd_open_on[i] = 1;
}

int
afr_nonblocking_entrylk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        afr_fd_ctx_t        *fd_ctx   = NULL;
        const char          *basename = NULL;
        loc_t               *loc      = NULL;
        int32_t call_count = 0;
        int i = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        initialize_entrylk_variables (frame, this);

        basename = int_lock->lk_basename;
        if (int_lock->lk_loc)
                loc = int_lock->lk_loc;

        if (local->fd) {
                fd_ctx = afr_fd_ctx_get (local->fd, this);
                if (!fd_ctx) {
                        gf_log (this->name, GF_LOG_INFO,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

                        return -1;
                }

                afr_mark_fd_open_on (local, fd_ctx, priv->child_count);
                call_count = internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                if (!call_count) {
                        gf_log (this->name, GF_LOG_INFO,
                                "fd not open on any subvolumes. aborting.");
                        afr_unlock (frame, this);
                        goto out;
                }

                /* Send non-blocking entrylk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] && local->fd_open_on[i]) {
                                afr_trace_entrylk_in (frame, AFR_ENTRYLK_NB_TRANSACTION,
                                                      AFR_LOCK_OP, basename, i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_entrylk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->fentrylk,
                                                   this->name, local->fd,
                                                   basename,
                                                   ENTRYLK_LOCK_NB, ENTRYLK_WRLCK);
                        }
                }
        } else {
                GF_ASSERT (loc);

                call_count = internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i]) {
                                afr_trace_entrylk_in (frame, AFR_ENTRYLK_NB_TRANSACTION,
                                                      AFR_LOCK_OP, basename, i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_entrylk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->entrylk,
                                                   this->name, loc, basename,
                                                   ENTRYLK_LOCK_NB, ENTRYLK_WRLCK);

                                if (!--call_count)
                                        break;

                        }
                }
        }
out:
        return 0;
}

int32_t
afr_nonblocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        int call_count  = 0;
        int child_index = (long) cookie;
        afr_fd_ctx_t        *fd_ctx = NULL;
        afr_private_t       *priv     = NULL;


        priv = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        afr_trace_inodelk_out (frame, AFR_INODELK_NB_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

        LOCK (&frame->lock);
        {
                call_count = --int_lock->lk_call_count;
        }
        UNLOCK (&frame->lock);

        if (op_ret < 0) {
                if (op_errno == ENOSYS) {
                        /* return ENOTSUP */
                        gf_log (this->name, GF_LOG_ERROR,
                                "subvolume does not support locking. "
                                "please load features/locks xlator on server");
                        local->op_ret                = op_ret;
                        int_lock->lock_op_ret        = op_ret;
                        int_lock->lock_op_errno      = op_errno;
                        local->op_errno              = op_errno;
                }
        } else {
                int_lock->inode_locked_nodes[child_index]
                        |= LOCKED_YES;
                int_lock->inodelk_lock_count++;

                if (priv->eager_lock && local->fd) {
                        fd_ctx = afr_fd_ctx_get (local->fd, this);
                        local->transaction.eager_lock[child_index] = 1;
                        /* piggybacked */

                        if (op_ret == 1) {
                                /* piggybacked */
                        } else if (op_ret == 0) {
                                /* lock acquired from server */
                                LOCK (&local->fd->lock);
                                {
                                        fd_ctx->lock_acquired[child_index]++;
                                }
                                UNLOCK (&local->fd->lock);
                        }
                }
        }

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Last inode locking reply received");
                /* all locks successful. Proceed to call FOP */
                if (int_lock->inodelk_lock_count ==
                                int_lock->lk_expected_count) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successful. Unlock and try locking
                   again, this time with serially blocking locks */
                else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "%d servers locked. Trying again with blocking calls",
                                int_lock->lock_count);

                        afr_unlock(frame, this);
                }
        }

        return 0;
}

int
afr_nonblocking_inodelk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        afr_fd_ctx_t        *fd_ctx   = NULL;
        int32_t  call_count = 0;
        int      i          = 0;
        int      ret        = 0;
        struct gf_flock flock = {0,};
        struct gf_flock full_flock = {0,};
        struct gf_flock *flock_use = &flock;
        int     piggyback = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        flock.l_start = int_lock->lk_flock.l_start;
        flock.l_len   = int_lock->lk_flock.l_len;
        flock.l_type  = int_lock->lk_flock.l_type;

        gf_log (this->name, GF_LOG_DEBUG, "attempting data lock range %"PRIu64
                " %"PRIu64" by %"PRIu64, flock.l_start, flock.l_len,
                frame->root->lk_owner);

        full_flock.l_type = int_lock->lk_flock.l_type;

        initialize_inodelk_variables (frame, this);

        if (local->fd) {
                fd_ctx = afr_fd_ctx_get (local->fd, this);
                if (!fd_ctx) {
                        gf_log (this->name, GF_LOG_INFO,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

                        ret = -1;
                        goto out;
                }

                afr_mark_fd_open_on (local, fd_ctx, priv->child_count);
                call_count = internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                if (!call_count) {
                        gf_log (this->name, GF_LOG_INFO,
                                "fd not open on any subvolumes. aborting.");
                        afr_unlock (frame, this);
                        goto out;
                }

                /* Send non-blocking inodelk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < priv->child_count; i++) {
                        if (!local->child_up[i] || !local->fd_open_on[i])
                                continue;

                        if (!priv->eager_lock)
                                goto wind;

                        flock_use = &full_flock;
                        piggyback = 0;

                        LOCK (&local->fd->lock);
                        {
                                if (fd_ctx->lock_acquired[i]) {
                                        fd_ctx->lock_piggyback[i]++;
                                        piggyback = 1;
                                }
                        }
                        UNLOCK (&local->fd->lock);

                        if (piggyback) {
                                /* (op_ret == 1) => indicate piggybacked lock */
                                afr_nonblocking_inodelk_cbk (frame, (void *) (long) i,
                                                             this, 1, 0);
                                if (!--call_count)
                                        break;
                                continue;
                        }
                wind:
                        afr_trace_inodelk_in (frame, AFR_INODELK_NB_TRANSACTION,
                                              AFR_LOCK_OP, flock_use, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_nonblocking_inodelk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->finodelk,
                                           this->name, local->fd,
                                           F_SETLK, flock_use);

                        if (!--call_count)
                                break;
                }
        } else {
                call_count = internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                for (i = 0; i < priv->child_count; i++) {
                        if (!local->child_up[i])
                                continue;
                        afr_trace_inodelk_in (frame, AFR_INODELK_NB_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_nonblocking_inodelk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->inodelk,
                                           this->name, &local->loc,
                                           F_SETLK, &flock);

                        if (!--call_count)
                                break;
                }
        }
out:
        return ret;
}

static int
__is_lower_locked (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        afr_local_t         *local    = NULL;
        int count = 0;
        int i     = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (int_lock->lower_locked_nodes[i] & LOCKED_LOWER)
                        count++;
        }

        return count;

}

static int
__is_higher_locked (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        afr_local_t         *local    = NULL;
        int count = 0;
        int i = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (int_lock->locked_nodes[i] & LOCKED_YES)
                        count++;
        }

        return count;

}

static int
afr_unlock_lower_entrylk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        const char          *basename = NULL;
        loc_t               *loc      = NULL;
        int call_count = 0;
        int i = -1;

        local = frame->local;
        int_lock = &local->internal_lock;
        priv  = this->private;

        basename = int_lock->lk_basename;
        if (int_lock->lk_loc)
                loc = int_lock->lk_loc;

        call_count = __is_lower_locked (frame, this);
        int_lock->lk_call_count = call_count;

        if (!call_count){
                gf_log (this->name, GF_LOG_TRACE,
                        "No internal locks unlocked");
                int_lock->lock_cbk (frame, this);
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (int_lock->lower_locked_nodes[i] & LOCKED_LOWER) {
                        afr_trace_entrylk_in (frame, AFR_ENTRYLK_NB_TRANSACTION,
                                              AFR_UNLOCK_OP, basename, i);

                        STACK_WIND_COOKIE (frame, afr_unlock_entrylk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->entrylk,
                                           this->name,
                                           loc, basename,
                                           ENTRYLK_UNLOCK, ENTRYLK_WRLCK);

                        if (!--call_count)
                                break;

                }
        }

out:
        return 0;

}


static int
afr_post_unlock_higher_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.done (frame, this);
        return 0;
}

static int
afr_post_unlock_lower_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_local_t         *local       = NULL;
        loc_t               *lower       = NULL;
        loc_t               *higher      = NULL;
        const char          *higher_name = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        higher = (lower == &local->transaction.parent_loc ?
                  &local->transaction.new_parent_loc :
                  &local->transaction.parent_loc);

        higher_name = (higher == &local->transaction.parent_loc ?
                       local->transaction.basename :
                       local->transaction.new_basename);

        if (__is_higher_locked (frame, this)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "unlocking higher");
                int_lock->lk_basename = higher_name;
                int_lock->lk_loc      = higher;
                int_lock->lock_cbk    = afr_post_unlock_higher_cbk;

                afr_unlock_entrylk (frame, this);
        } else
                local->transaction.done (frame, this);

        return 0;
}

static int
afr_rename_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_local_t         *local       = NULL;
        loc_t               *lower       = NULL;
        const char          *lower_name  = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        lower_name = (lower == &local->transaction.parent_loc ?
                      local->transaction.basename :
                      local->transaction.new_basename);

        if (__is_lower_locked (frame, this)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "unlocking lower");
                int_lock->lk_basename = lower_name;
                int_lock->lk_loc      = lower;
                int_lock->lock_cbk    = afr_post_unlock_lower_cbk;

                afr_unlock_lower_entrylk (frame, this);
        } else
                afr_post_unlock_lower_cbk (frame, this);

        return 0;
}

static int
afr_rename_transaction (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        return (local->transaction.type ==
                AFR_ENTRY_RENAME_TRANSACTION);

}

int32_t
afr_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        if (transaction_lk_op (local)) {
                if (is_afr_lock_transaction (local))
                        afr_unlock_inodelk (frame, this);
                else
                        if (!afr_rename_transaction (frame, this))
                                afr_unlock_entrylk (frame, this);
                        else
                                afr_rename_unlock (frame, this);
        } else {
                if (is_afr_lock_selfheal (local))
                        afr_unlock_inodelk (frame, this);
                else
                        afr_unlock_entrylk (frame, this);
        }

        return 0;
}

int
afr_mark_locked_nodes (xlator_t *this, fd_t *fd,
                       unsigned char *locked_nodes)
{
        afr_private_t *priv  = NULL;
        afr_fd_ctx_t  *fdctx = NULL;
        uint64_t       tmp   = 0;
        int            ret   = 0;

        priv = this->private;

        ret = afr_fd_ctx_set (this, fd);
        if (ret)
                goto out;

        ret = fd_ctx_get (fd, this, &tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "failed to get the fd ctx");
                goto out;
        }
        fdctx = (afr_fd_ctx_t *) (long) tmp;

        GF_ASSERT (fdctx->locked_on);

        memcpy (fdctx->locked_on, locked_nodes,
                priv->child_count);

out:
        return ret;
}

static int
__is_fd_saved (xlator_t *this, fd_t *fd)
{
        afr_locked_fd_t *locked_fd = NULL;
        afr_private_t   *priv      = NULL;
        int              found     = 0;

        priv = this->private;

        list_for_each_entry (locked_fd, &priv->saved_fds, list) {
                if (locked_fd->fd == fd) {
                        found = 1;
                        break;
                }
        }

        return found;
}

static int
__afr_save_locked_fd (xlator_t *this, fd_t *fd)
{
        afr_private_t   *priv      = NULL;
        afr_locked_fd_t *locked_fd = NULL;
        int              ret       = 0;

        priv = this->private;

        locked_fd = GF_CALLOC (1, sizeof (*locked_fd),
                               gf_afr_mt_locked_fd);
        if (!locked_fd) {
                ret = -1;
                goto out;
        }

        locked_fd->fd = fd;
        INIT_LIST_HEAD (&locked_fd->list);

        list_add_tail (&locked_fd->list, &priv->saved_fds);

out:
        return ret;
}

int
afr_save_locked_fd (xlator_t *this, fd_t *fd)
{
        afr_private_t   *priv      = NULL;
        int              ret       = 0;

        priv = this->private;

        pthread_mutex_lock (&priv->mutex);
        {
                if (__is_fd_saved (this, fd)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "fd=%p already saved", fd);
                        goto unlock;
                }

                ret = __afr_save_locked_fd (this, fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_INFO,
                                "fd=%p could not be saved", fd);
                        goto unlock;
                }
        }
unlock:
        pthread_mutex_unlock (&priv->mutex);

        return ret;
}

static int
afr_lock_recovery_cleanup (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_locked_fd_t *locked_fd = NULL;

        local = frame->local;

        locked_fd = local->locked_fd;

        STACK_DESTROY (frame->root);
        afr_local_cleanup (local, this);

        afr_save_locked_fd (this, locked_fd->fd);

        return 0;

}

static int
afr_get_source_lock_recovery (xlator_t *this, fd_t *fd)
{
        afr_fd_ctx_t  *fdctx        = NULL;
        afr_private_t *priv         = NULL;
        uint64_t      tmp           = 0;
        int           i             = 0;
        int           source_child  = -1;
        int           ret           = 0;

        priv = this->private;

        ret = fd_ctx_get (fd, this, &tmp);
        if (ret)
                goto out;

        fdctx = (afr_fd_ctx_t *) (long) tmp;

        for (i = 0; i < priv->child_count; i++) {
                if (fdctx->locked_on[i]) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Found lock recovery source=%d", i);
                        source_child = i;
                        break;
                }
        }

out:
        return source_child;

}

int32_t
afr_get_locks_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct gf_flock *lock);
int32_t
afr_recover_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int32_t source_child = 0;
        struct gf_flock flock   = {0,};

        local = frame->local;
        priv  = this->private;

        if (op_ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "lock recovery failed");
                goto cleanup;
        }

        source_child = local->source_child;

        memcpy (&flock, lock, sizeof (*lock));

        STACK_WIND_COOKIE (frame, afr_get_locks_fd_cbk,
                           (void *) (long) source_child,
                           priv->children[source_child],
                           priv->children[source_child]->fops->lk,
                           local->fd, F_GETLK_FD, &flock);

        return 0;

cleanup:
        afr_lock_recovery_cleanup (frame, this);
        return 0;
}

int
afr_recover_lock (call_frame_t *frame, xlator_t *this,
                  struct gf_flock *flock)
{
        afr_local_t   *local             = NULL;
        afr_private_t *priv              = NULL;
        int32_t      lock_recovery_child = 0;

        priv  = this->private;
        local = frame->local;

        lock_recovery_child = local->lock_recovery_child;

        frame->root->lk_owner = flock->l_owner;

        STACK_WIND_COOKIE (frame, afr_recover_lock_cbk,
                           (void *) (long) lock_recovery_child,
                           priv->children[lock_recovery_child],
                           priv->children[lock_recovery_child]->fops->lk,
                           local->fd, F_SETLK, flock);

        return 0;
}

static int
is_afr_lock_eol (struct gf_flock *lock)
{
        int ret = 0;

        if ((lock->l_type == GF_LK_EOL))
                ret = 1;

        return ret;
}

int32_t
afr_get_locks_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
        if (op_ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "Failed to get locks on fd");
                goto cleanup;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Got a lock on fd");

        if (is_afr_lock_eol (lock)) {
                gf_log (this->name, GF_LOG_INFO,
                        "Reached EOL on locks on fd");
                goto cleanup;
        }

        afr_recover_lock (frame, this, lock);

        return 0;

cleanup:
        afr_lock_recovery_cleanup (frame, this);

        return 0;
}

static int
afr_lock_recovery (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local        = NULL;
        afr_private_t *priv         = NULL;
        fd_t          *fd           = NULL;
        int            ret          = 0;
        int32_t        source_child = 0;
        struct gf_flock   flock        = {0,};

        priv  = this->private;
        local = frame->local;

        fd = local->fd;

        source_child = afr_get_source_lock_recovery (this, fd);
        if (source_child < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not recover locks due to lock "
                        "split brain");
                ret = -1;
                goto out;
        }

        local->source_child = source_child;

        /* the flock can be zero filled as we're querying incrementally
           the locks held on the fd.
        */
        STACK_WIND_COOKIE (frame, afr_get_locks_fd_cbk,
                           (void *) (long) source_child,
                           priv->children[source_child],
                           priv->children[source_child]->fops->lk,
                           local->fd, F_GETLK_FD, &flock);

out:
        return ret;
}


static int
afr_mark_fd_opened (xlator_t *this, fd_t *fd, int32_t child_index)
{
        afr_fd_ctx_t *fdctx = NULL;
        uint64_t      tmp   = 0;
        int           ret   = 0;

        ret = fd_ctx_get (fd, this, &tmp);
        if (ret)
                goto out;

        fdctx = (afr_fd_ctx_t *) (long) tmp;

        fdctx->opened_on[child_index] = AFR_FD_OPENED;

out:
        return ret;
}

int32_t
afr_lock_recovery_preopen_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                               int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        int32_t child_index = (long )cookie;
        int ret = 0;

        if (op_ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "Reopen during lock-recovery failed");
                goto cleanup;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Open succeeded => proceed to recover locks");

        ret = afr_lock_recovery (frame, this);
        if (ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "Lock recovery failed");
                goto cleanup;
        }

        ret = afr_mark_fd_opened (this, fd, child_index);
        if (ret) {
                gf_log (this->name, GF_LOG_INFO,
                        "Marking fd open failed");
                goto cleanup;
        }

        return 0;

cleanup:
        afr_lock_recovery_cleanup (frame, this);
        return 0;
}

static int
afr_lock_recovery_preopen (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv  = NULL;
        afr_local_t   *local = NULL;
        uint64_t       tmp   = 0;
        afr_fd_ctx_t  *fdctx = NULL;
        loc_t          loc   = {0,};
        int32_t        child_index = 0;
        int            ret   = 0;

        priv  = this->private;
        local = frame->local;

        GF_ASSERT (local && local->fd);

        ret = fd_ctx_get (local->fd, this, &tmp);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get the context of fd",
                        uuid_utoa (local->fd->inode->gfid));
        fdctx = (afr_fd_ctx_t *) (long) tmp;
        /* TODO: instead we should return from the function */
        GF_ASSERT (fdctx);

        child_index = local->lock_recovery_child;

        inode_path (local->fd->inode, NULL, (char **)&loc.path);
        loc.name   = strrchr (loc.path, '/');
        loc.inode  = inode_ref (local->fd->inode);
        loc.parent = inode_parent (local->fd->inode, 0, NULL);


        STACK_WIND_COOKIE (frame, afr_lock_recovery_preopen_cbk,
                           (void *)(long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->open,
                           &loc, fdctx->flags, local->fd,
                           fdctx->wbflags);

        return 0;
}

static int
is_fd_opened (fd_t *fd, int32_t child_index)
{
        afr_fd_ctx_t *fdctx = NULL;
        uint64_t      tmp = 0;
        int           ret = 0;

        ret = fd_ctx_get (fd, THIS, &tmp);
        if (ret)
                goto out;

        fdctx = (afr_fd_ctx_t *) (long) tmp;

        if (fdctx->opened_on[child_index] == AFR_FD_OPENED)
                ret = 1;

out:
        return ret;
}

int
afr_attempt_lock_recovery (xlator_t *this, int32_t child_index)
{
        call_frame_t    *frame     = NULL;
        afr_private_t   *priv      = NULL;
        afr_local_t     *local     = NULL;
        afr_locked_fd_t *locked_fd = NULL;
        afr_locked_fd_t  *tmp       = NULL;
        int              ret       = 0;
        struct list_head locks_list = {0,};


        priv = this->private;

        if (list_empty (&priv->saved_fds))
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local),
                           gf_afr_mt_afr_local_t);
        if (!local) {
                ret = -1;
                goto out;
        }

        AFR_LOCAL_INIT (local, priv);
        if (!local) {
                ret = -1;
                goto out;
        }

        frame->local = local;

        INIT_LIST_HEAD (&locks_list);

        pthread_mutex_lock (&priv->mutex);
        {
                list_splice_init (&priv->saved_fds, &locks_list);
        }
        pthread_mutex_unlock (&priv->mutex);

        list_for_each_entry_safe (locked_fd, tmp,
                                  &locks_list, list) {

                list_del_init (&locked_fd->list);

                local->fd                  = fd_ref (locked_fd->fd);
                local->lock_recovery_child = child_index;
                local->locked_fd           = locked_fd;

                if (!is_fd_opened (locked_fd->fd, child_index)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "attempting open before lock "
                                "recovery");
                        afr_lock_recovery_preopen (frame, this);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "attempting lock recovery "
                                "without a preopen");
                        afr_lock_recovery (frame, this);
                }
        }

out:
        return ret;
}

void
afr_lk_transfer_datalock (call_frame_t *dst, call_frame_t *src,
                          unsigned int child_count)
{
        afr_local_t *dst_local = NULL;
        afr_local_t *src_local = NULL;
        afr_internal_lock_t *dst_lock = NULL;
        afr_internal_lock_t *src_lock = NULL;

        dst_local = dst->local;
        dst_lock  = &dst_local->internal_lock;
        src_local = src->local;
        src_lock  = &src_local->internal_lock;
        if (src_lock->inode_locked_nodes) {
                memcpy (dst_lock->inode_locked_nodes,
                        src_lock->inode_locked_nodes,
                        sizeof (*dst_lock->inode_locked_nodes) * child_count);
                memset (src_lock->inode_locked_nodes, 0,
                        sizeof (*src_lock->inode_locked_nodes) * child_count);
        }

        dst_lock->inodelk_lock_count = src_lock->inodelk_lock_count;
        src_lock->inodelk_lock_count = 0;
}
