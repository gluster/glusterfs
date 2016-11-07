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

#include "afr.h"
#include "afr-transaction.h"
#include "afr-messages.h"

#include <signal.h>


#define LOCKED_NO       0x0        /* no lock held */
#define LOCKED_YES      0x1        /* for DATA, METADATA, ENTRY and higher_path */
#define LOCKED_LOWER    0x2        /* for lower path */

#define AFR_TRACE_INODELK_IN(frame, this, params ...)           \
        do {                                                    \
                afr_private_t *_priv = this->private;           \
                if (!_priv->inodelk_trace)                      \
                        break;                                  \
                afr_trace_inodelk_in (frame, this, params);     \
        } while (0);

#define AFR_TRACE_INODELK_OUT(frame, this, params ...)          \
        do {                                                    \
                afr_private_t *_priv = this->private;           \
                if (!_priv->inodelk_trace)                      \
                        break;                                  \
                afr_trace_inodelk_out (frame, this, params);    \
        } while (0);

#define AFR_TRACE_ENTRYLK_IN(frame, this, params ...)           \
        do {                                                    \
                afr_private_t *_priv = this->private;           \
                if (!_priv->entrylk_trace)                      \
                        break;                                  \
                afr_trace_entrylk_in (frame, this, params);     \
        } while (0);

#define AFR_TRACE_ENTRYLK_OUT(frame, this, params ...)          \
        do {                                                    \
                afr_private_t *_priv = this->private;           \
                if (!_priv->entrylk_trace)                      \
                        break;                                  \
                afr_trace_entrylk_out (frame, this, params);    \
        } while (0);

int
afr_entry_lockee_cmp (const void *l1, const void *l2)
{
        const afr_entry_lockee_t       *r1 = l1;
        const afr_entry_lockee_t       *r2 = l2;
        int                            ret = 0;
        uuid_t                         gfid1 = {0};
        uuid_t                         gfid2 = {0};

        loc_gfid ((loc_t*)&r1->loc, gfid1);
        loc_gfid ((loc_t*)&r2->loc, gfid2);
        ret = gf_uuid_compare (gfid1, gfid2);
        /*Entrylks with NULL basename are the 'smallest'*/
        if (ret == 0) {
                if (!r1->basename)
                        return -1;
                if (!r2->basename)
                        return 1;
                ret = strcmp (r1->basename, r2->basename);
        }

        if (ret <= 0)
                return -1;
        else
                return 1;
}

int afr_lock_blocking (call_frame_t *frame, xlator_t *this, int child_index);

static int
afr_copy_locked_nodes (call_frame_t *frame, xlator_t *this);

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
afr_set_lk_owner (call_frame_t *frame, xlator_t *this, void *lk_owner)
{
        gf_msg_trace (this->name, 0,
                      "Setting lk-owner=%llu",
                      (unsigned long long) (unsigned long)lk_owner);

        set_lk_owner_from_ptr (&frame->root->lk_owner, lk_owner);
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

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i])
                        ++call_count;
        }

        return call_count;
}

static void
afr_print_inodelk (char *str, int size, int cmd,
                   struct gf_flock *flock, gf_lkowner_t *owner)
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
                  "start=%llu, len=%llu, pid=%llu, lk-owner=%s",
                  cmd_str, type_str, (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid,
                  lkowner_utoa (owner));

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
                   gf_lkowner_t *owner)
{
        snprintf (str, size, "Basename=%s, lk-owner=%s",
                  basename ? basename : "<nul>",
                  lkowner_utoa (owner));
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
afr_trace_inodelk_out (call_frame_t *frame, xlator_t *this,
                       afr_lock_call_type_t lock_call_type,
                       afr_lock_op_type_t lk_op_type, struct gf_flock *flock,
                       int op_ret, int op_errno, int32_t child_index)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        char lockee[256];
        char lock_call_type_str[256];
        char verdict[16];

        local    = frame->local;
        int_lock = &local->internal_lock;

        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        afr_print_verdict (op_ret, op_errno, verdict);

        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_LOCK_INFO,
                "[%s %s] [%s] lk-owner=%s Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REPLY" : "UNLOCK REPLY",
                verdict, lkowner_utoa (&frame->root->lk_owner), lockee,
                (unsigned long long) int_lock->lock_number);

}

static void
afr_trace_inodelk_in (call_frame_t *frame, xlator_t *this,
                      afr_lock_call_type_t lock_call_type,
                      afr_lock_op_type_t lk_op_type, struct gf_flock *flock,
                      int32_t cmd, int32_t child_index)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];

        local    = frame->local;
        int_lock = &local->internal_lock;

        afr_print_inodelk (lock, 256, cmd, flock, &frame->root->lk_owner);
        afr_print_lockee (lockee, 256, &local->loc, local->fd, child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_LOCK_INFO,
                "[%s %s] Lock={%s} Lockee={%s} Number={%llu}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REQUEST" : "UNLOCK REQUEST",
                lock, lockee,
                (unsigned long long) int_lock->lock_number);

}

static void
afr_trace_entrylk_in (call_frame_t *frame, xlator_t *this,
                      afr_lock_call_type_t lock_call_type,
                      afr_lock_op_type_t lk_op_type, const char *basename,
                      int32_t cookie)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        int                 child_index = 0;
        int                 lockee_no = 0;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->entrylk_trace) {
                return;
        }
        lockee_no = cookie / priv->child_count;
        child_index = cookie % priv->child_count;

        afr_print_entrylk (lock, 256, basename, &frame->root->lk_owner);
        afr_print_lockee (lockee, 256, &int_lock->lockee[lockee_no].loc, local->fd,
                          child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_LOCK_INFO,
                "[%s %s] Lock={%s} Lockee={%s} Number={%llu}, Cookie={%d}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REQUEST" : "UNLOCK REQUEST",
                lock, lockee,
                (unsigned long long) int_lock->lock_number,
                cookie);
}

static void
afr_trace_entrylk_out (call_frame_t *frame, xlator_t *this,
                       afr_lock_call_type_t lock_call_type,
                       afr_lock_op_type_t lk_op_type, const char *basename,
                       int op_ret, int op_errno, int32_t cookie)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        int                 lockee_no = 0;
        int                 child_index = 0;

        char lock[256];
        char lockee[256];
        char lock_call_type_str[256];
        char verdict[16];

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        if (!priv->entrylk_trace) {
                return;
        }
        lockee_no = cookie / priv->child_count;
        child_index = cookie % priv->child_count;

        afr_print_entrylk (lock, 256, basename, &frame->root->lk_owner);
        afr_print_lockee (lockee, 256, &int_lock->lockee[lockee_no].loc, local->fd,
                          child_index);

        afr_set_lock_call_type (lock_call_type, lock_call_type_str, int_lock);

        afr_print_verdict (op_ret, op_errno, verdict);

        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_LOCK_INFO,
                "[%s %s] [%s] Lock={%s} Lockee={%s} Number={%llu} Cookie={%d}",
                lock_call_type_str,
                lk_op_type == AFR_LOCK_OP ? "LOCK REPLY" : "UNLOCK REPLY",
                verdict,
                lock, lockee,
                (unsigned long long) int_lock->lock_number,
                cookie);

}

static int
transaction_lk_op (afr_local_t *local)
{
        afr_internal_lock_t *int_lock = NULL;
        int ret = -1;

        int_lock = &local->internal_lock;

        if (int_lock->transaction_lk_type == AFR_TRANSACTION_LK) {
                gf_msg_debug (THIS->name, 0,
                              "lk op is for a transaction");
                ret = 1;
        }
        else if (int_lock->transaction_lk_type == AFR_SELFHEAL_LK) {
                gf_msg_debug (THIS->name, 0,
                              "lk op is for a self heal");

                ret = 0;
        }

        if (ret == -1)
                gf_msg_debug (THIS->name, 0,
                              "lk op is not set");

        return ret;

}

int
afr_is_inodelk_transaction(afr_local_t *local)
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

int
afr_init_entry_lockee (afr_entry_lockee_t *lockee, afr_local_t *local,
                       loc_t *loc, char *basename, int child_count)
{
        int     ret     = -1;

        loc_copy (&lockee->loc, loc);
        lockee->basename        = (basename)? gf_strdup (basename): NULL;
        if (basename && !lockee->basename)
                goto out;

        lockee->locked_count    = 0;
        lockee->locked_nodes    = GF_CALLOC (child_count,
                                             sizeof (*lockee->locked_nodes),
                                             gf_afr_mt_afr_node_character);

        if (!lockee->locked_nodes)
                goto out;

        ret = 0;
out:
        return ret;

}

void
afr_entry_lockee_cleanup (afr_internal_lock_t *int_lock)
{
        int     i   = 0;

        for (i = 0; i < int_lock->lockee_count; i++) {
                loc_wipe (&int_lock->lockee[i].loc);
                if (int_lock->lockee[i].basename)
                        GF_FREE (int_lock->lockee[i].basename);
                if (int_lock->lockee[i].locked_nodes)
                       GF_FREE (int_lock->lockee[i].locked_nodes);
        }

        return;
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

        for (i = 0; i < AFR_LOCKEE_COUNT_MAX; i++) {
                if (!int_lock->lockee[i].locked_nodes)
                        break;
                int_lock->lockee[i].locked_count = 0;
                memset (int_lock->lockee[i].locked_nodes, 0,
                        sizeof (*int_lock->lockee[i].locked_nodes) *
                        priv->child_count);
        }

        return 0;
}

static int
initialize_inodelk_variables (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_private_t       *priv     = NULL;
        afr_inodelk_t       *inodelk  = NULL;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        inodelk = afr_get_inodelk (int_lock, int_lock->domain);

        inodelk->lock_count    = 0;
        int_lock->lk_attempted_count = 0;
        int_lock->lock_op_ret   = -1;
        int_lock->lock_op_errno = 0;

        memset (inodelk->locked_nodes, 0,
                sizeof (*inodelk->locked_nodes) * priv->child_count);
        memset (int_lock->locked_nodes, 0,
                sizeof (*int_lock->locked_nodes) * priv->child_count);

        return 0;
}

int
afr_lockee_locked_nodes_count (afr_internal_lock_t *int_lock)
{
        int call_count  = 0;
        int i           = 0;

        for (i = 0; i < int_lock->lockee_count; i++)
                call_count += int_lock->lockee[i].locked_count;

        return call_count;
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
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
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
                gf_msg_trace (this->name, 0,
                              "All internal locks unlocked");

                int_lock->lock_cbk (frame, this);
        }

        return 0;
}

void
afr_update_uninodelk (afr_local_t *local, afr_internal_lock_t *int_lock,
                    int32_t child_index)
{
        afr_inodelk_t       *inodelk = NULL;

        inodelk = afr_get_inodelk (int_lock, int_lock->domain);
        inodelk->locked_nodes[child_index] &= LOCKED_NO;
        if (local->transaction.eager_lock)
                local->transaction.eager_lock[child_index] = 0;

}

static int32_t
afr_unlock_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t         *local = NULL;
        afr_internal_lock_t *int_lock = NULL;
        int32_t             child_index = (long)cookie;
        afr_private_t       *priv = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;

        AFR_TRACE_INODELK_OUT (frame, this, AFR_INODELK_TRANSACTION,
                               AFR_UNLOCK_OP, NULL, op_ret,
                               op_errno, child_index);

        priv = this->private;

        if (op_ret < 0 && op_errno != ENOTCONN && op_errno != EBADFD) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        AFR_MSG_UNLOCK_FAIL,
                        "path=%s gfid=%s: unlock failed on subvolume %s "
                        "with lock owner %s", local->loc.path,
                        loc_gfid_utoa (&(local->loc)),
                        priv->children[child_index]->name,
                        lkowner_utoa (&frame->root->lk_owner));
        }

        afr_update_uninodelk (local, int_lock, child_index);

        afr_unlock_common_cbk (frame, cookie, this, op_ret, op_errno, xdata);

        return 0;

}

static int
afr_unlock_inodelk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_inodelk_t       *inodelk  = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        struct gf_flock flock = {0,};
        struct gf_flock full_flock = {0,};
        struct gf_flock *flock_use = NULL;
        int call_count = 0;
        int i = 0;
        int piggyback = 0;
        afr_fd_ctx_t        *fd_ctx      = NULL;


        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        inodelk = afr_get_inodelk (int_lock, int_lock->domain);

        flock.l_start = inodelk->flock.l_start;
        flock.l_len   = inodelk->flock.l_len;
        flock.l_type  = F_UNLCK;

        full_flock.l_type = F_UNLCK;
        call_count = afr_locked_nodes_count (inodelk->locked_nodes,
                                             priv->child_count);

        int_lock->lk_call_count = call_count;

        if (!call_count) {
                gf_msg_trace (this->name, 0,
                              "No internal locks unlocked");

                int_lock->lock_cbk (frame, this);
                goto out;
        }

        if (local->fd)
                fd_ctx = afr_fd_ctx_get (local->fd, this);

        for (i = 0; i < priv->child_count; i++) {
                if ((inodelk->locked_nodes[i] & LOCKED_YES) != LOCKED_YES)
                        continue;

                if (local->fd) {
                        flock_use = &flock;
                        if (!local->transaction.eager_lock[i]) {
                                goto wind;
                        }

                        piggyback = 0;

                        LOCK (&local->fd->lock);
                        {
                                if (fd_ctx->lock_piggyback[i]) {
                                        fd_ctx->lock_piggyback[i]--;
                                        piggyback = 1;
                                } else {
                                        fd_ctx->lock_acquired[i]--;
                                }
                        }
                        UNLOCK (&local->fd->lock);

                        if (piggyback) {
                                afr_unlock_inodelk_cbk (frame, (void *) (long) i,
                                                        this, 1, 0, NULL);
                                if (!--call_count)
                                        break;
                                continue;
                        }

                        flock_use = &full_flock;
                wind:
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_TRANSACTION,
                                              AFR_UNLOCK_OP, flock_use, F_SETLK,
                                              i);

                        STACK_WIND_COOKIE (frame, afr_unlock_inodelk_cbk,
                                           (void *) (long)i,
                                           priv->children[i],
                                           priv->children[i]->fops->finodelk,
                                           int_lock->domain, local->fd,
                                           F_SETLK, flock_use, NULL);

                        if (!--call_count)
                                break;

                } else {
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_TRANSACTION,
                                              AFR_UNLOCK_OP, &flock, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_unlock_inodelk_cbk,
                                           (void *) (long)i,
                                           priv->children[i],
                                           priv->children[i]->fops->inodelk,
                                           int_lock->domain, &local->loc,
                                           F_SETLK, &flock, NULL);

                        if (!--call_count)
                                break;
                }
        }
out:
        return 0;
}

static int32_t
afr_unlock_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t         *local = NULL;
        afr_private_t       *priv  = NULL;
        afr_internal_lock_t *int_lock = NULL;
        int32_t             child_index = 0;
        int                 lockee_no   = 0;

        priv = this->private;
        lockee_no = (int)((long) cookie) / priv->child_count;
        child_index = (int) ((long) cookie) % priv->child_count;

        local = frame->local;
        int_lock = &local->internal_lock;

        AFR_TRACE_ENTRYLK_OUT (frame, this, AFR_ENTRYLK_TRANSACTION,
                               AFR_UNLOCK_OP,
                               int_lock->lockee[lockee_no].basename, op_ret,
                               op_errno, (int) ((long)cookie));

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        AFR_MSG_ENTRY_UNLOCK_FAIL,
                        "%s: unlock failed on %s", local->loc.path,
                        priv->children[child_index]->name);
        }

        int_lock->lockee[lockee_no].locked_nodes[child_index] &= LOCKED_NO;
        afr_unlock_common_cbk (frame, cookie, this, op_ret, op_errno, NULL);

        return 0;
}

static int
afr_unlock_entrylk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t     *int_lock       = NULL;
        afr_local_t             *local          = NULL;
        afr_private_t           *priv           = NULL;
        int                     call_count      = 0;
        int                     index           = 0;
        int                     lockee_no       = 0;
        int                     copies          = 0;
        int                     i               = -1;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;
        copies   = priv->child_count;

        call_count = afr_lockee_locked_nodes_count (int_lock);

        int_lock->lk_call_count = call_count;

        if (!call_count){
                gf_msg_trace (this->name, 0,
                              "No internal locks unlocked");
                int_lock->lock_cbk (frame, this);
                goto out;
        }

        for (i = 0; i < int_lock->lockee_count * priv->child_count; i++) {
                lockee_no = i / copies;
                index     = i % copies;
                if (int_lock->lockee[lockee_no].locked_nodes[index] & LOCKED_YES) {
                        AFR_TRACE_ENTRYLK_IN (frame, this, AFR_ENTRYLK_NB_TRANSACTION,
                                              AFR_UNLOCK_OP,
                                              int_lock->lockee[lockee_no].basename,
                                              i);

                        STACK_WIND_COOKIE (frame, afr_unlock_entrylk_cbk,
                                           (void *) (long) i,
                                           priv->children[index],
                                           priv->children[index]->fops->entrylk,
                                           int_lock->domain,
                                           &int_lock->lockee[lockee_no].loc,
                                           int_lock->lockee[lockee_no].basename,
                                           ENTRYLK_UNLOCK, ENTRYLK_WRLCK, NULL);

                        if (!--call_count)
                                break;
                }
        }

out:
        return 0;

}

static int32_t
afr_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_internal_lock_t     *int_lock       = NULL;
        afr_local_t             *local          = NULL;
        afr_private_t           *priv           = NULL;
        int                     cky             = (long) cookie;
        int                     child_index     = 0;
        int                     lockee_no       = 0;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        child_index = ((int)cky) % priv->child_count;
        lockee_no   = ((int)cky) / priv->child_count;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        if (op_errno == ENOSYS) {
                                /* return ENOTSUP */
                                gf_msg (this->name, GF_LOG_ERROR, ENOSYS,
                                        AFR_MSG_LOCK_XLATOR_NOT_LOADED,
                                        "subvolume does not support locking. "
                                        "please load features/locks xlator on server");
                                local->op_ret = op_ret;
                                int_lock->lock_op_ret = op_ret;
                        }

                        local->op_errno              = op_errno;
                        int_lock->lock_op_errno      = op_errno;
                }

		int_lock->lk_attempted_count++;
        }
        UNLOCK (&frame->lock);

        if ((op_ret == -1) &&
            (op_errno == ENOSYS)) {
                afr_unlock (frame, this);
        } else {
                if (op_ret == 0) {
                        if (local->transaction.type == AFR_ENTRY_TRANSACTION ||
                            local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) {
                                int_lock->lockee[lockee_no].locked_nodes[child_index] |= LOCKED_YES;
                                int_lock->lockee[lockee_no].locked_count++;
                                int_lock->entrylk_lock_count++;
                        } else {
                                int_lock->locked_nodes[child_index] |= LOCKED_YES;
                                int_lock->lock_count++;
                        }
                }
                afr_lock_blocking (frame, this, cky + 1);
        }

        return 0;
}

static int32_t
afr_blocking_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        AFR_TRACE_INODELK_OUT (frame, this, AFR_INODELK_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

        afr_lock_cbk (frame, cookie, this, op_ret, op_errno, xdata);
        return 0;

}

static int32_t
afr_blocking_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        AFR_TRACE_ENTRYLK_OUT (frame, this, AFR_ENTRYLK_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long)cookie);

        afr_lock_cbk (frame, cookie, this, op_ret, op_errno, xdata);
        return 0;
}

static int
afr_copy_locked_nodes (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_inodelk_t       *inodelk  = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                inodelk = afr_get_inodelk (int_lock, int_lock->domain);
                memcpy (inodelk->locked_nodes, int_lock->locked_nodes,
                        sizeof (*inodelk->locked_nodes) * priv->child_count);
                inodelk->lock_count = int_lock->lock_count;
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
        case AFR_ENTRY_TRANSACTION:
                /*entrylk_count is being used in both non-blocking and blocking
                 * modes */
                break;
        }

        return 0;

}

static gf_boolean_t
afr_is_entrylk (afr_internal_lock_t *int_lock,
                afr_transaction_type trans_type)
{
        gf_boolean_t is_entrylk = _gf_false;

        if ((int_lock->transaction_lk_type == AFR_SELFHEAL_LK) &&
            int_lock->selfheal_lk_type == AFR_ENTRY_SELF_HEAL_LK) {

                is_entrylk = _gf_true;

        } else if ((int_lock->transaction_lk_type == AFR_TRANSACTION_LK) &&
                 (trans_type == AFR_ENTRY_TRANSACTION ||
                  trans_type == AFR_ENTRY_RENAME_TRANSACTION)) {

                is_entrylk = _gf_true;

        } else {
                is_entrylk = _gf_false;
        }

        return is_entrylk;
}

static gf_boolean_t
_is_lock_wind_needed (afr_local_t *local, int child_index)
{
        if (!local->child_up[child_index])
                return _gf_false;

        return _gf_true;
}

static void
afr_log_entry_locks_failure(xlator_t *this, afr_local_t *local,
                            afr_internal_lock_t *int_lock)
{
        const char *fop = NULL;
        char *pargfid = NULL;
        const char *name = NULL;

        fop = gf_fop_list[local->op];

        switch (local->op) {
        case GF_FOP_LINK:
                pargfid = uuid_utoa(local->newloc.pargfid);
                name = local->newloc.name;
                break;
        default:
                pargfid = uuid_utoa(local->loc.pargfid);
                name = local->loc.name;
                break;
        }

        gf_msg (this->name, GF_LOG_WARNING, 0, AFR_MSG_BLOCKING_LKS_FAILED,
                "Unable to obtain sufficient blocking entry locks on at least "
                "one child while attempting %s on {pgfid:%s, name:%s}.", fop,
                pargfid, name);
}

static gf_boolean_t
is_blocking_locks_count_sufficient (call_frame_t *frame, xlator_t *this)
{
        afr_local_t  *local = NULL;
        afr_private_t *priv = NULL;
        afr_internal_lock_t *int_lock = NULL;
        gf_boolean_t is_entrylk = _gf_false;
        int child = 0;
        int nlockee = 0;
        int lockee_count = 0;
        gf_boolean_t ret = _gf_true;

        local = frame->local;
        priv = this->private;
        int_lock = &local->internal_lock;
        lockee_count = int_lock->lockee_count;
        is_entrylk = afr_is_entrylk (int_lock, local->transaction.type);

        if (!is_entrylk) {
                if (int_lock->lock_count == 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_BLOCKING_LKS_FAILED, "Unable to obtain "
                                "blocking inode lock on even one child for "
                                "gfid:%s.", uuid_utoa (local->inode->gfid));
                        return _gf_false;
                } else {
                        /*inodelk succeded on atleast one child. */
                        return _gf_true;
                }

        } else {
                if (int_lock->entrylk_lock_count == 0) {
                        afr_log_entry_locks_failure (this, local, int_lock);
                        return _gf_false;
                }
                /* For FOPS that take multiple sets of locks (mkdir, rename),
                 * there must be atleast one brick on which the locks from
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
                        afr_log_entry_locks_failure (this, local, int_lock);
        }

        return ret;

}

int
afr_lock_blocking (call_frame_t *frame, xlator_t *this, int cookie)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_inodelk_t       *inodelk     = NULL;
        afr_local_t         *local       = NULL;
        afr_private_t       *priv        = NULL;
        struct gf_flock flock = {0,};
        uint64_t ctx = 0;
        int ret = 0;
        int child_index = 0;
        int lockee_no   = 0;
        gf_boolean_t is_entrylk = _gf_false;

        local         = frame->local;
        int_lock      = &local->internal_lock;
        priv          = this->private;
        child_index   = cookie % priv->child_count;
        lockee_no     = cookie / priv->child_count;
        is_entrylk    = afr_is_entrylk (int_lock, local->transaction.type);


        if (!is_entrylk) {
                inodelk = afr_get_inodelk (int_lock, int_lock->domain);
                flock.l_start = inodelk->flock.l_start;
                flock.l_len   = inodelk->flock.l_len;
                flock.l_type  = inodelk->flock.l_type;
        }

        if (local->fd) {
                ret = fd_ctx_get (local->fd, this, &ctx);

                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_FD_CTX_GET_FAILED,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;

                        afr_copy_locked_nodes (frame, this);

                        afr_unlock (frame, this);

                        return 0;
                }
        }

        if (int_lock->lk_expected_count == int_lock->lk_attempted_count) {
                if (!is_blocking_locks_count_sufficient (frame, this)) {

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;

                        afr_copy_locked_nodes (frame, this);

                        afr_unlock(frame, this);

                        return 0;
                }
        }

        if (int_lock->lk_expected_count == int_lock->lk_attempted_count) {
                /* we're done locking */

                gf_msg_debug (this->name, 0,
                              "we're done locking");

                afr_copy_locked_nodes (frame, this);

                int_lock->lock_op_ret = 0;
                int_lock->lock_cbk (frame, this);
                return 0;
        }

        if (!_is_lock_wind_needed (local, child_index)) {
                afr_lock_blocking (frame, this, cookie + 1);
                return 0;
        }

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:

                if (local->fd) {
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLKW,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_inodelk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->finodelk,
                                           int_lock->domain, local->fd,
                                           F_SETLKW, &flock, NULL);

                } else {
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLKW,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_inodelk_cbk,
                                           (void *) (long) child_index,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->inodelk,
                                           int_lock->domain, &local->loc,
                                           F_SETLKW, &flock, NULL);
                }

                break;

        case AFR_ENTRY_RENAME_TRANSACTION:
        case AFR_ENTRY_TRANSACTION:
                /*Accounting for child_index increments on 'down'
                 *and 'fd-less' children */

                if (local->fd) {
                        AFR_TRACE_ENTRYLK_IN (frame, this, AFR_ENTRYLK_TRANSACTION,
                                              AFR_LOCK_OP,
                                              int_lock->lockee[lockee_no].basename,
                                              cookie);

                        STACK_WIND_COOKIE (frame, afr_blocking_entrylk_cbk,
                                           (void *) (long) cookie,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->fentrylk,
                                           int_lock->domain, local->fd,
                                           int_lock->lockee[lockee_no].basename,
                                           ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
                } else {
                        AFR_TRACE_ENTRYLK_IN (frame, this,
                                              AFR_ENTRYLK_TRANSACTION,
                                              AFR_LOCK_OP, local->transaction.basename,
                                              child_index);

                        STACK_WIND_COOKIE (frame, afr_blocking_entrylk_cbk,
                                           (void *) (long) cookie,
                                           priv->children[child_index],
                                           priv->children[child_index]->fops->entrylk,
                                           int_lock->domain,
                                           &int_lock->lockee[lockee_no].loc,
                                           int_lock->lockee[lockee_no].basename,
                                           ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
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
        case AFR_ENTRY_TRANSACTION:
                up_count = AFR_COUNT (local->child_up, priv->child_count);
                int_lock->lk_call_count = int_lock->lk_expected_count
                                        = (int_lock->lockee_count *
                                           up_count);
                initialize_entrylk_variables (frame, this);
                break;
        }

        afr_lock_blocking (frame, this, 0);

        return 0;
}

static int32_t
afr_nonblocking_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        int call_count          = 0;
        int child_index         = (long) cookie;
        int copies              = 0;
        int index               = 0;
        int lockee_no           = 0;
        afr_private_t       *priv = NULL;

        priv = this->private;

        copies = priv->child_count;
        index = child_index % copies;
        lockee_no = child_index / copies;

        local    = frame->local;
        int_lock = &local->internal_lock;

        AFR_TRACE_ENTRYLK_OUT (frame, this, AFR_ENTRYLK_TRANSACTION,
                               AFR_LOCK_OP,
                               int_lock->lockee[lockee_no].basename, op_ret,
                               op_errno, (long) cookie);

	LOCK (&frame->lock);
	{
		if (op_ret < 0 ) {
			if (op_errno == ENOSYS) {
                        /* return ENOTSUP */
			        gf_msg (this->name, GF_LOG_ERROR,
                                        ENOSYS, AFR_MSG_LOCK_XLATOR_NOT_LOADED,
                                        "subvolume does not support "
                                        "locking. please load features/locks"
                                        " xlator on server");
				local->op_ret         = op_ret;
				int_lock->lock_op_ret = op_ret;

				int_lock->lock_op_errno      = op_errno;
				local->op_errno              = op_errno;
			}
		} else if (op_ret == 0) {
			int_lock->lockee[lockee_no].locked_nodes[index] |= \
				LOCKED_YES;
			int_lock->lockee[lockee_no].locked_count++;
			int_lock->entrylk_lock_count++;
		}

                call_count = --int_lock->lk_call_count;
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                gf_msg_trace (this->name, 0,
                              "Last locking reply received");
                /* all locks successful. Proceed to call FOP */
                if (int_lock->entrylk_lock_count ==
                                int_lock->lk_expected_count) {
                        gf_msg_trace (this->name, 0,
                                      "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successful. Unlock and try locking
                   again, this time with serially blocking locks */
                else {
                        gf_msg_trace (this->name, 0,
                                      "%d servers locked. Trying again "
                                      "with blocking calls",
                                      int_lock->lock_count);

                        afr_unlock(frame, this);
                }
        }

        return 0;
}

int
afr_nonblocking_entrylk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock   = NULL;
        afr_local_t         *local      = NULL;
        afr_private_t       *priv       = NULL;
        afr_fd_ctx_t        *fd_ctx     = NULL;
        int                 copies      = 0;
        int                 index       = 0;
        int                 lockee_no   = 0;
        int32_t             call_count  = 0;
        int i = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        copies = priv->child_count;
        initialize_entrylk_variables (frame, this);

        if (local->fd) {
                fd_ctx = afr_fd_ctx_get (local->fd, this);
                if (!fd_ctx) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_FD_CTX_GET_FAILED,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

			afr_unlock (frame, this);
                        return -1;
                }

                call_count = int_lock->lockee_count * internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                if (!call_count) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_INFO_COMMON,
                                "fd not open on any subvolumes. aborting.");
                        afr_unlock (frame, this);
                        goto out;
                }

                /* Send non-blocking entrylk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < int_lock->lockee_count*priv->child_count; i++) {
                        index = i%copies;
                        lockee_no = i/copies;
                        if (local->child_up[index]) {
                                AFR_TRACE_ENTRYLK_IN (frame, this, AFR_ENTRYLK_NB_TRANSACTION,
                                                      AFR_LOCK_OP,
                                                      int_lock->lockee[lockee_no].basename,
                                                      i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_entrylk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[index],
                                                   priv->children[index]->fops->fentrylk,
                                                   this->name, local->fd,
                                                   int_lock->lockee[lockee_no].basename,
                                                   ENTRYLK_LOCK_NB, ENTRYLK_WRLCK,
                                                   NULL);
                                if (!--call_count)
                                        break;
                        }
                }
        } else {
                call_count = int_lock->lockee_count * internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                for (i = 0; i < int_lock->lockee_count*priv->child_count; i++) {
                        index = i%copies;
                        lockee_no = i/copies;
                        if (local->child_up[index]) {
                                AFR_TRACE_ENTRYLK_IN (frame, this, AFR_ENTRYLK_NB_TRANSACTION,
                                                      AFR_LOCK_OP,
                                                      int_lock->lockee[lockee_no].basename,
                                                      i);

                                STACK_WIND_COOKIE (frame, afr_nonblocking_entrylk_cbk,
                                                   (void *) (long) i,
                                                   priv->children[index],
                                                   priv->children[index]->fops->entrylk,
                                                   this->name, &int_lock->lockee[lockee_no].loc,
                                                   int_lock->lockee[lockee_no].basename,
                                                   ENTRYLK_LOCK_NB, ENTRYLK_WRLCK,
                                                   NULL);

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
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_inodelk_t       *inodelk  = NULL;
        afr_local_t         *local    = NULL;
        int call_count  = 0;
        int child_index = (long) cookie;
        afr_fd_ctx_t        *fd_ctx = NULL;


        local    = frame->local;
        int_lock = &local->internal_lock;
        inodelk = afr_get_inodelk (int_lock, int_lock->domain);

        AFR_TRACE_INODELK_OUT (frame, this, AFR_INODELK_NB_TRANSACTION,
                               AFR_LOCK_OP, NULL, op_ret,
                               op_errno, (long) cookie);

	if (local->fd)
		fd_ctx = afr_fd_ctx_get (local->fd, this);

        LOCK (&frame->lock);
        {
		if (op_ret < 0) {
			if (op_errno == ENOSYS) {
				/* return ENOTSUP */
		                gf_msg (this->name, GF_LOG_ERROR, ENOSYS,
                                        AFR_MSG_LOCK_XLATOR_NOT_LOADED,
					"subvolume does not support "
                                        "locking. please load features/locks"
                                        " xlator on server");
				local->op_ret                = op_ret;
				int_lock->lock_op_ret        = op_ret;
				int_lock->lock_op_errno      = op_errno;
				local->op_errno              = op_errno;
			}
			if (local->transaction.eager_lock)
				local->transaction.eager_lock[child_index] = 0;
		} else {
			inodelk->locked_nodes[child_index] |= LOCKED_YES;
			inodelk->lock_count++;

			if (local->transaction.eager_lock &&
			    local->transaction.eager_lock[child_index] &&
			    local->fd) {
				/* piggybacked */
				if (op_ret == 1) {
					/* piggybacked */
				} else if (op_ret == 0) {
					/* lock acquired from server */
                                        fd_ctx->lock_acquired[child_index]++;
				}
			}
		}

                call_count = --int_lock->lk_call_count;
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                gf_msg_trace (this->name, 0,
                              "Last inode locking reply received");
                /* all locks successful. Proceed to call FOP */
                if (inodelk->lock_count == int_lock->lk_expected_count) {
                        gf_msg_trace (this->name, 0,
                                      "All servers locked. Calling the cbk");
                        int_lock->lock_op_ret = 0;
                        int_lock->lock_cbk (frame, this);
                }
                /* Not all locks were successful. Unlock and try locking
                   again, this time with serially blocking locks */
                else {
                        gf_msg_trace (this->name, 0,
                                      "%d servers locked. "
                                      "Trying again with blocking calls",
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
        afr_inodelk_t       *inodelk  = NULL;
        afr_local_t         *local    = NULL;
        afr_private_t       *priv     = NULL;
        afr_fd_ctx_t        *fd_ctx   = NULL;
        int32_t             call_count = 0;
        int                 i          = 0;
        int                 ret        = 0;
        struct              gf_flock flock = {0,};
        struct              gf_flock full_flock = {0,};
        struct              gf_flock *flock_use = NULL;
        int                 piggyback = 0;

        local    = frame->local;
        int_lock = &local->internal_lock;
        priv     = this->private;

        inodelk = afr_get_inodelk (int_lock, int_lock->domain);

        flock.l_start = inodelk->flock.l_start;
        flock.l_len   = inodelk->flock.l_len;
        flock.l_type  = inodelk->flock.l_type;

        full_flock.l_type = inodelk->flock.l_type;

        initialize_inodelk_variables (frame, this);

        if (local->fd) {
                fd_ctx = afr_fd_ctx_get (local->fd, this);
                if (!fd_ctx) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_FD_CTX_GET_FAILED,
                                "unable to get fd ctx for fd=%p",
                                local->fd);

                        local->op_ret           = -1;
                        int_lock->lock_op_ret   = -1;
                        local->op_errno         = EINVAL;
                        int_lock->lock_op_errno = EINVAL;

			afr_unlock (frame, this);
                        ret = -1;
                        goto out;
                }

                call_count = internal_lock_count (frame, this);
                int_lock->lk_call_count = call_count;
                int_lock->lk_expected_count = call_count;

                if (!call_count) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_SUBVOLS_DOWN,
                                "All bricks are down, aborting.");
                        afr_unlock (frame, this);
                        goto out;
                }

                /* Send non-blocking inodelk calls only on up children
                   and where the fd has been opened */
                for (i = 0; i < priv->child_count; i++) {
                        if (!local->child_up[i])
                                continue;

                        flock_use = &flock;
                        if (!local->transaction.eager_lock_on) {
                                goto wind;
                        }

                        piggyback = 0;
                        local->transaction.eager_lock[i] = 1;

			afr_set_delayed_post_op (frame, this);

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
                                                             this, 1, 0, NULL);
                                if (!--call_count)
                                        break;
                                continue;
                        }
                        flock_use = &full_flock;
                wind:
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_NB_TRANSACTION,
                                              AFR_LOCK_OP, flock_use, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_nonblocking_inodelk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->finodelk,
                                           int_lock->domain, local->fd,
                                           F_SETLK, flock_use, NULL);

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
                        AFR_TRACE_INODELK_IN (frame, this,
                                              AFR_INODELK_NB_TRANSACTION,
                                              AFR_LOCK_OP, &flock, F_SETLK, i);

                        STACK_WIND_COOKIE (frame, afr_nonblocking_inodelk_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->inodelk,
                                           int_lock->domain, &local->loc,
                                           F_SETLK, &flock, NULL);

                        if (!--call_count)
                                break;
                }
        }
out:
        return ret;
}

int32_t
afr_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        if (transaction_lk_op (local)) {
                if (afr_is_inodelk_transaction(local))
                        afr_unlock_inodelk (frame, this);
                else
                        afr_unlock_entrylk (frame, this);

        } else {
                if (is_afr_lock_selfheal (local))
                        afr_unlock_inodelk (frame, this);
                else
                        afr_unlock_entrylk (frame, this);
        }

        return 0;
}

int
afr_lk_transfer_datalock (call_frame_t *dst, call_frame_t *src, char *dom,
                          unsigned int child_count)
{
        afr_local_t         *dst_local   = NULL;
        afr_local_t         *src_local   = NULL;
        afr_internal_lock_t *dst_lock    = NULL;
        afr_internal_lock_t *src_lock    = NULL;
        afr_inodelk_t       *dst_inodelk = NULL;
        afr_inodelk_t       *src_inodelk = NULL;
        int                 ret = -1;

        src_local = src->local;
        src_lock  = &src_local->internal_lock;
        src_inodelk = afr_get_inodelk (src_lock, dom);
        dst_local = dst->local;
        dst_lock  = &dst_local->internal_lock;
        dst_inodelk = afr_get_inodelk (dst_lock, dom);
        if (!dst_inodelk || !src_inodelk)
                goto out;
        if (src_inodelk->locked_nodes) {
                memcpy (dst_inodelk->locked_nodes, src_inodelk->locked_nodes,
                        sizeof (*dst_inodelk->locked_nodes) * child_count);
                memset (src_inodelk->locked_nodes, 0,
                        sizeof (*src_inodelk->locked_nodes) * child_count);
        }

        dst_lock->transaction_lk_type = src_lock->transaction_lk_type;
        dst_lock->selfheal_lk_type    = src_lock->selfheal_lk_type;
        dst_inodelk->lock_count = src_inodelk->lock_count;
        src_inodelk->lock_count = 0;
        ret = 0;
out:
        return ret;
}
