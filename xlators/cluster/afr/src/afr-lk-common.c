/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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
        if (!frame->root->lk_owner) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Setting lk-owner=%llu",
                        (unsigned long long) (unsigned long)frame->root);
                frame->root->lk_owner = (uint64_t) (unsigned long)frame->root;
        }
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
internal_lock_count (call_frame_t *frame, xlator_t *this,
                     afr_fd_ctx_t *fd_ctx)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;

        int32_t call_count = 0;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        if (fd_ctx) {
                GF_ASSERT (local->fd);
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] && fd_ctx->opened_on[i])
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
                   struct flock *flock, uint64_t owner)
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
                       afr_lock_op_type_t lk_op_type, struct flock *flock,
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

        gf_log (this->name, GF_LOG_NORMAL,
                "[%s %s] [%s] Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REPLY" : "UNLOCK REPLY",
                verdict,
                lockee,
                (unsigned long long) int_lock->lock_number);

}

static void
afr_trace_inodelk_in (call_frame_t *frame, afr_lock_call_type_t lock_call_type,
                      afr_lock_op_type_t lk_op_type, struct flock *flock,
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

        gf_log (this->name, GF_LOG_NORMAL,
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

        gf_log (this->name, GF_LOG_NORMAL,
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

        gf_log (this->name, GF_LOG_NORMAL,
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
        int i;
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
        afr_trace_inodelk_out (frame, AFR_INODELK_TRANSACTION,
                               AFR_UNLOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

        if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Unlock failed for some reason");
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

        struct flock flock;
        int call_count = 0;
        int i = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        flock.l_start = int_lock->lk_flock.l_start;
        flock.l_len   = int_lock->lk_flock.l_len;
        flock.l_type  = F_UNLCK;

        call_count = afr_locked_nodes_count (int_lock->inode_locked_nodes,
                                             priv->child_count);

        int_lock->lk_call_count = call_count;

        if (!call_count) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No internal locks unlocked");
                int_lock->lock_cbk (frame, this);
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (int_lock->inode_locked_nodes[i] & LOCKED_YES) {
                        if (local->fd) {
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

        }

out:
        return 0;
}

static int32_t
afr_unlock_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        afr_trace_entrylk_out (frame, AFR_ENTRYLK_TRANSACTION,
                              AFR_UNLOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

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
	afr_private_t       *priv     = NULL;

	int done = 0;
	int child_index = (long) cookie;

	local    = frame->local;
        int_lock = &local->internal_lock;
	priv     = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			if (op_errno == ENOSYS) {
				/* return ENOTSUP */
				gf_log (this->name, GF_LOG_ERROR,
					"subvolume does not support locking. "
					"please load features/posix-locks xlator on server");
				local->op_ret = op_ret;
                                int_lock->lock_op_ret = op_ret;
				done = 1;
			}

			local->child_up[child_index] = 0;
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
                        int_lock->locked_nodes[child_index]
                                |= LOCKED_YES;
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
        const char          *lower_name  = NULL;
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
                                        "please load features/posix-locks xlator on server");

                                local->op_ret   = op_ret;
                        }

                        local->child_up[child_index] = 0;
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

        lower_name = (lower == &local->transaction.parent_loc ?
                      local->transaction.basename :
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
        afr_fd_ctx_t        *fd_ctx      = NULL;
	loc_t               *lower       = NULL;
	loc_t               *higher      = NULL;
	const char          *lower_name  = NULL;
	const char          *higher_name = NULL;

	struct flock flock;
        uint64_t ctx;
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
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

                        afr_copy_locked_nodes (frame, this);

                        afr_unlock (frame, this);

                        return 0;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                /* skip over children that or down
                   or don't have the fd open */

                while ((child_index < priv->child_count)
                       && (!local->child_up[child_index]
                           || !fd_ctx->opened_on[child_index]))

                        child_index++;
        } else {
                /* skip over children that are down */
                while ((child_index < priv->child_count)
                       && !local->child_up[child_index])
                        child_index++;
        }

	if ((child_index == priv->child_count) &&
	    int_lock->lock_count == 0) {

		gf_log (this->name, GF_LOG_DEBUG,
			"unable to lock on even one child");

		local->op_ret           = -1;
                int_lock->lock_op_ret   = -1;
		local->op_errno         = EAGAIN;
                int_lock->lock_op_errno = EAGAIN;

                afr_copy_locked_nodes (frame, this);

                afr_unlock(frame, this);

		return 0;

	}

	if ((child_index == priv->child_count)
	    || (int_lock->lock_count ==
		afr_up_children_count (priv->child_count,
                                       local->child_up))) {

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

		higher = (lower == &local->transaction.parent_loc ?
			  &local->transaction.new_parent_loc :
			  &local->transaction.parent_loc);

		higher_name = (higher == &local->transaction.parent_loc ?
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

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        switch (local->transaction.type) {
	case AFR_DATA_TRANSACTION:
	case AFR_METADATA_TRANSACTION:
                initialize_inodelk_variables (frame, this);
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
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
	afr_private_t       *priv     = NULL;

        int call_count          = 0;
	int child_index         = (long) cookie;

	local    = frame->local;
        int_lock = &local->internal_lock;
	priv     = this->private;

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
					"please load features/posix-locks xlator on server");
				local->op_ret         = op_ret;
                                int_lock->lock_op_ret = op_ret;

                                local->child_up[child_index] = 0;
                                int_lock->lock_op_errno      = op_errno;
                                local->op_errno              = op_errno;
                        }
        } else if (op_ret == 0) {
                        int_lock->entry_locked_nodes[child_index]
                                |= LOCKED_YES;
                        int_lock->entrylk_lock_count++;
        }

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Last locking reply received");
                /* all locks successfull. Proceed to call FOP */
                if (int_lock->entrylk_lock_count ==
                    afr_up_children_count (priv->child_count, local->child_up)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successfull. Unlock and try locking
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
        uint64_t  ctx;
        int ret = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        initialize_entrylk_variables (frame, this);

        basename = int_lock->lk_basename;
        if (int_lock->lk_loc)
                loc = int_lock->lk_loc;

        if (local->fd) {
                ret = fd_ctx_get (local->fd, this, &ctx);

                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

                        return -1;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                call_count = internal_lock_count (frame, this, fd_ctx);
                int_lock->lk_call_count = call_count;

                /* Send non-blocking entrylk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] && fd_ctx->opened_on[i]) {
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

                call_count = internal_lock_count (frame, this, NULL);
                int_lock->lk_call_count = call_count;

                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i]) {
                                afr_trace_entrylk_in (frame, AFR_ENTRYLK_NB_TRANSACTION,
                                                      AFR_LOCK_OP, basename, i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_entrylk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->entrylk,
                                                   this->name, loc, basename,
                                                   ENTRYLK_LOCK, ENTRYLK_WRLCK);

                                if (!--call_count)
                                        break;

                        }
                }
        }

        return 0;
}

int32_t
afr_nonblocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno)
{
        afr_internal_lock_t *int_lock = NULL;
	afr_local_t         *local    = NULL;
	afr_private_t       *priv     = NULL;

        int call_count  = 0;
	int child_index = (long) cookie;

	local    = frame->local;
        int_lock = &local->internal_lock;
	priv     = this->private;

        afr_trace_inodelk_out (frame, AFR_INODELK_NB_TRANSACTION,
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
					"please load features/posix-locks xlator on server");
				local->op_ret                = op_ret;
                                int_lock->lock_op_ret        = op_ret;
                                local->child_up[child_index] = 0;
                                int_lock->lock_op_errno      = op_errno;
                                local->op_errno              = op_errno;
                        }
        } else if (op_ret == 0) {
                        int_lock->inode_locked_nodes[child_index]
                                |= LOCKED_YES;
                        int_lock->inodelk_lock_count++;
        }

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Last inode locking reply received");
                /* all locks successfull. Proceed to call FOP */
                if (int_lock->inodelk_lock_count ==
                    afr_up_children_count (priv->child_count, local->child_up)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successfull. Unlock and try locking
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
        uint64_t ctx        = 0;
        int      i          = 0;
        int      ret        = 0;
        struct flock flock;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

	flock.l_start = int_lock->lk_flock.l_start;
	flock.l_len   = int_lock->lk_flock.l_len;
	flock.l_type  = int_lock->lk_flock.l_type;

        initialize_inodelk_variables (frame, this);

        if (local->fd) {
                ret = fd_ctx_get (local->fd, this, &ctx);

                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

                        ret = -1;
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                call_count = internal_lock_count (frame, this, fd_ctx);
                int_lock->lk_call_count = call_count;

                /* Send non-blocking inodelk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] && fd_ctx->opened_on[i]) {
                                afr_trace_inodelk_in (frame, AFR_INODELK_NB_TRANSACTION,
                                                      AFR_LOCK_OP, &flock, F_SETLK, i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_inodelk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[i],
                                                   priv->children[i]->fops->finodelk,
                                                   this->name, local->fd,
                                                   F_SETLK, &flock);

                                if (!--call_count)
                                        break;

                        }

                }
        } else {
                call_count = internal_lock_count (frame, this, NULL);
                int_lock->lk_call_count = call_count;

                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i]) {
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
	const char          *lower_name  = NULL;
	const char          *higher_name = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        lower_name = (lower == &local->transaction.parent_loc ?
                      local->transaction.basename :
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
	loc_t               *higher      = NULL;
	const char          *lower_name  = NULL;
	const char          *higher_name = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        lower_name = (lower == &local->transaction.parent_loc ?
                      local->transaction.basename :
                      local->transaction.new_basename);

        higher = (lower == &local->transaction.parent_loc ?
                  &local->transaction.new_parent_loc :
                  &local->transaction.parent_loc);

        higher_name = (higher == &local->transaction.parent_loc ?
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
