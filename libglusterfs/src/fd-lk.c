/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "fd-lk.h"
#include "common-utils.h"
#include "libglusterfs-messages.h"

int32_t
_fd_lk_delete_lock (fd_lk_ctx_node_t *lock)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("fd-lk", lock, out);

        list_del_init (&lock->next);

        ret = 0;
out:
        return ret;
}

int32_t
_fd_lk_destroy_lock (fd_lk_ctx_node_t *lock)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("fd-lk", lock, out);

        GF_FREE (lock);

        ret = 0;
out:
        return ret;
}

int
_fd_lk_destroy_lock_list (fd_lk_ctx_t *lk_ctx)
{
        int               ret     = -1;
        fd_lk_ctx_node_t *lk      = NULL;
        fd_lk_ctx_node_t *tmp     = NULL;

        GF_VALIDATE_OR_GOTO ("fd-lk", lk_ctx, out);

        list_for_each_entry_safe (lk, tmp, &lk_ctx->lk_list, next) {
                _fd_lk_delete_lock (lk);
                _fd_lk_destroy_lock (lk);
        }
        ret = 0;
out:
        return ret;
}

int
fd_lk_ctx_unref (fd_lk_ctx_t *lk_ctx)
{
        int ref = -1;

        GF_VALIDATE_OR_GOTO ("fd-lk", lk_ctx, err);

        LOCK (&lk_ctx->lock);
        {
                ref = --lk_ctx->ref;
                if (ref < 0)
                        GF_ASSERT (!ref);
                if (ref == 0)
                        _fd_lk_destroy_lock_list (lk_ctx);
        }
        UNLOCK (&lk_ctx->lock);

        if (ref == 0) {
                LOCK_DESTROY (&lk_ctx->lock);
                GF_FREE (lk_ctx);
        }

        return 0;
err:
        return -1;
}

fd_lk_ctx_t *
_fd_lk_ctx_ref (fd_lk_ctx_t *lk_ctx)
{
        if (!lk_ctx) {
                gf_msg_callingfn ("fd-lk", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        ++lk_ctx->ref;

        return lk_ctx;
}

fd_lk_ctx_t *
fd_lk_ctx_ref (fd_lk_ctx_t *lk_ctx)
{
        fd_lk_ctx_t *new_lk_ctx = NULL;

        if (!lk_ctx) {
                gf_msg_callingfn ("fd-lk", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        LOCK (&lk_ctx->lock);
        {
                new_lk_ctx = _fd_lk_ctx_ref (lk_ctx);
        }
        UNLOCK (&lk_ctx->lock);

        return new_lk_ctx;
}

fd_lk_ctx_t *
fd_lk_ctx_try_ref (fd_lk_ctx_t *lk_ctx)
{
        int         ret         = -1;
        fd_lk_ctx_t *new_lk_ctx = NULL;

        if (!lk_ctx) {
                goto out;
        }

        ret = TRY_LOCK (&lk_ctx->lock);
        if (ret)
                goto out;

        new_lk_ctx = _fd_lk_ctx_ref (lk_ctx);
        UNLOCK (&lk_ctx->lock);

out:
        return new_lk_ctx;
}

fd_lk_ctx_t *
fd_lk_ctx_create ()
{
        fd_lk_ctx_t *fd_lk_ctx = NULL;

        fd_lk_ctx = GF_CALLOC (1, sizeof (fd_lk_ctx_t),
                               gf_common_mt_fd_lk_ctx_t);
        if (!fd_lk_ctx)
                goto out;

        INIT_LIST_HEAD (&fd_lk_ctx->lk_list);

        LOCK_INIT (&fd_lk_ctx->lock);

        fd_lk_ctx = fd_lk_ctx_ref (fd_lk_ctx);
out:
        return fd_lk_ctx;
}

int
_fd_lk_insert_lock (fd_lk_ctx_t *lk_ctx,
                    fd_lk_ctx_node_t *lock)
{
        list_add_tail (&lock->next, &lk_ctx->lk_list);
        return 0;
}

static off_t
_fd_lk_get_lock_len (off_t start, off_t end)
{
        if (end == LLONG_MAX)
                return 0;
        else
                return (end - start + 1);
}

fd_lk_ctx_node_t *
fd_lk_ctx_node_new (int32_t cmd, struct gf_flock *flock)
{
        fd_lk_ctx_node_t  *new_lock  = NULL;

        /* TODO: get from mem-pool */
        new_lock = GF_CALLOC (1, sizeof (fd_lk_ctx_node_t),
                              gf_common_mt_fd_lk_ctx_node_t);
        if (!new_lock)
                goto out;

        new_lock->cmd = cmd;

        if (flock) {
                new_lock->fl_type  = flock->l_type;
                new_lock->fl_start = flock->l_start;

                if (flock->l_len == 0)
                        new_lock->fl_end = LLONG_MAX;
                else
                        new_lock->fl_end = flock->l_start + flock->l_len - 1;

                memcpy (&new_lock->user_flock, flock,
                        sizeof (struct gf_flock));
        }

        INIT_LIST_HEAD (&new_lock->next);
out:
        return new_lock;
}

int32_t
_fd_lk_delete_unlck_locks (fd_lk_ctx_t *lk_ctx)
{
        int32_t            ret   = -1;
        fd_lk_ctx_node_t  *tmp   = NULL;
        fd_lk_ctx_node_t  *lk    = NULL;

        GF_VALIDATE_OR_GOTO ("fd-lk", lk_ctx, out);

        list_for_each_entry_safe (lk, tmp, &lk_ctx->lk_list, next) {
                if (lk->fl_type == F_UNLCK) {
                        _fd_lk_delete_lock (lk);
                        _fd_lk_destroy_lock (lk);
                }
        }
out:
        return ret;
}

int
fd_lk_overlap (fd_lk_ctx_node_t *l1,
               fd_lk_ctx_node_t *l2)
{
        if (l1->fl_end >= l2->fl_start &&
            l2->fl_end >= l1->fl_start)
                return 1;

        return 0;
}

fd_lk_ctx_node_t *
_fd_lk_add_locks (fd_lk_ctx_node_t *l1,
                  fd_lk_ctx_node_t *l2)
{
        fd_lk_ctx_node_t  *sum = NULL;

        sum = fd_lk_ctx_node_new (0, NULL);
        if (!sum)
                goto out;

        sum->fl_start = min (l1->fl_start, l2->fl_start);
        sum->fl_end   = max (l1->fl_end, l2->fl_end);

        sum->user_flock.l_start = sum->fl_start;
        sum->user_flock.l_len   = _fd_lk_get_lock_len (sum->fl_start,
                                                       sum->fl_end);
out:
        return sum;
}

/* Subtract two locks */
struct _values {
        fd_lk_ctx_node_t *locks[3];
};

int32_t
_fd_lk_sub_locks (struct _values *v,
                  fd_lk_ctx_node_t *big,
                  fd_lk_ctx_node_t *small)
{
        int32_t  ret  = -1;

        if ((big->fl_start == small->fl_start) &&
            (big->fl_end   == small->fl_end)) {
                /* both edges coincide with big */
                v->locks[0] = fd_lk_ctx_node_new (small->cmd, NULL);
                if (!v->locks[0])
                        goto out;

                memcpy (v->locks[0], big, sizeof (fd_lk_ctx_node_t));

                v->locks[0]->fl_type            = small->fl_type;
                v->locks[0]->user_flock.l_type = small->fl_type;
        } else if ((small->fl_start > big->fl_start) &&
                   (small->fl_end   < big->fl_end)) {
                /* small lock is completely inside big lock,
                   break it down into 3 different locks. */
                v->locks[0] = fd_lk_ctx_node_new (big->cmd, NULL);
                if (!v->locks[0])
                        goto out;

                v->locks[1] = fd_lk_ctx_node_new (small->cmd, NULL);
                if (!v->locks[1])
                        goto out;

                v->locks[2] = fd_lk_ctx_node_new (big->cmd, NULL);
                if (!v->locks[2])
                        goto out;

                memcpy (v->locks[0], big, sizeof (fd_lk_ctx_node_t));
                v->locks[0]->fl_end = small->fl_start - 1;
                v->locks[0]->user_flock.l_len =
                        _fd_lk_get_lock_len (v->locks[0]->fl_start,
                                             v->locks[0]->fl_end);

                memcpy (v->locks[1], small, sizeof (fd_lk_ctx_node_t));

                memcpy (v->locks[2], big, sizeof (fd_lk_ctx_node_t));
                v->locks[2]->fl_start = small->fl_end + 1;
                v->locks[2]->user_flock.l_len =
                        _fd_lk_get_lock_len (v->locks[2]->fl_start,
                                             v->locks[2]->fl_end);
        } else if (small->fl_start == big->fl_start) {
                /* One of the ends co-incide, break the
                   locks into two separate parts */
                v->locks[0] = fd_lk_ctx_node_new (small->cmd, NULL);
                if (!v->locks[0])
                        goto out;

                v->locks[1] = fd_lk_ctx_node_new (big->cmd, NULL);
                if (!v->locks[1])
                        goto out;

                memcpy (v->locks[0], small, sizeof (fd_lk_ctx_node_t));

                memcpy (v->locks[1], big, sizeof (fd_lk_ctx_node_t));
                v->locks[1]->fl_start = small->fl_end + 1;
                v->locks[1]->user_flock.l_start = small->fl_end + 1;
        } else if (small->fl_end == big->fl_end) {
                /* One of the ends co-incide, break the
                   locks into two separate parts */
                v->locks[0] = fd_lk_ctx_node_new (small->cmd, NULL);
                if (!v->locks[0])
                        goto out;

                v->locks[1] = fd_lk_ctx_node_new (big->cmd, NULL);
                if (!v->locks[1])
                        goto out;

                memcpy (v->locks[0], big, sizeof (fd_lk_ctx_node_t));
                v->locks[0]->fl_end = small->fl_start - 1;
                v->locks[0]->user_flock.l_len =
                        _fd_lk_get_lock_len (v->locks[0]->fl_start,
                                             v->locks[0]->fl_end);

                memcpy (v->locks[1], small, sizeof (fd_lk_ctx_node_t));
        } else {
                /* We should never come to this case */
                GF_ASSERT (!"Invalid case");
        }
        ret = 0;
out:
        return ret;
}

static void
_fd_lk_insert_and_merge (fd_lk_ctx_t *lk_ctx,
                         fd_lk_ctx_node_t *lock)
{
        int32_t               ret     = -1;
        int32_t               i       = 0;
        fd_lk_ctx_node_t     *entry   = NULL;
        fd_lk_ctx_node_t     *t       = NULL;
        fd_lk_ctx_node_t     *sum     = NULL;
        struct _values        v       = {.locks = {0, 0, 0 }};

        list_for_each_entry_safe (entry, t, &lk_ctx->lk_list, next) {
                if (!fd_lk_overlap (entry, lock))
                        continue;

                if (entry->fl_type == lock->fl_type) {
                        sum = _fd_lk_add_locks (entry, lock);
                        if (!sum)
                                return;
                        sum->fl_type = entry->fl_type;
                        sum->user_flock.l_type = entry->fl_type;
                        _fd_lk_delete_lock (entry);
                        _fd_lk_destroy_lock (entry);
                        _fd_lk_destroy_lock (lock);
                        _fd_lk_insert_and_merge (lk_ctx, sum);
                        return;
                } else {
                        sum = _fd_lk_add_locks (entry, lock);
                        sum->fl_type = lock->fl_type;
                        sum->user_flock.l_type = lock->fl_type;
                        ret = _fd_lk_sub_locks (&v, sum, lock);
                        if (ret)
                                return;
                        _fd_lk_delete_lock (entry);
                        _fd_lk_destroy_lock (entry);

                        _fd_lk_delete_lock (lock);
                        _fd_lk_destroy_lock (lock);

                        _fd_lk_destroy_lock (sum);

                        for (i = 0; i < 3; i++) {
                                if (!v.locks[i])
                                        continue;

                                INIT_LIST_HEAD (&v.locks[i]->next);
                                _fd_lk_insert_and_merge (lk_ctx, v.locks[i]);
                        }
                        _fd_lk_delete_unlck_locks (lk_ctx);
                        return;
                }
        }

        /* no conflicts, so just insert */
        if (lock->fl_type != F_UNLCK) {
                _fd_lk_insert_lock (lk_ctx, lock);
        } else {
                _fd_lk_destroy_lock (lock);
        }
}

static void
print_lock_list (fd_lk_ctx_t *lk_ctx)
{
        fd_lk_ctx_node_t    *lk     = NULL;

        gf_msg_debug ("fd-lk", 0, "lock list:");

        list_for_each_entry (lk, &lk_ctx->lk_list, next)
                gf_msg_debug ("fd-lk", 0, "owner = %s, cmd = %s fl_type = %s,"
                              " fs_start = %"PRId64", fs_end = %"PRId64", "
                              "user_flock: l_type = %s, l_start = %"PRId64", "
                              "l_len = %"PRId64", ",
                              lkowner_utoa (&lk->user_flock.l_owner),
                              get_lk_cmd (lk->cmd), get_lk_type (lk->fl_type),
                              lk->fl_start, lk->fl_end,
                              get_lk_type (lk->user_flock.l_type),
                              lk->user_flock.l_start, lk->user_flock.l_len);
}

int
fd_lk_insert_and_merge (fd_t *fd, int32_t cmd,
                        struct gf_flock *flock)
{
        int32_t              ret      = -1;
        fd_lk_ctx_t         *lk_ctx   = NULL;
        fd_lk_ctx_node_t    *lk     = NULL;

        GF_VALIDATE_OR_GOTO ("fd-lk", fd, out);
        GF_VALIDATE_OR_GOTO ("fd-lk", flock, out);

        lk_ctx = fd_lk_ctx_ref (fd->lk_ctx);
        lk     = fd_lk_ctx_node_new (cmd, flock);

        gf_msg_debug ("fd-lk", 0, "new lock request: owner = %s, fl_type = %s"
                      ", fs_start = %"PRId64", fs_end = %"PRId64", user_flock:"
                      " l_type = %s, l_start = %"PRId64", l_len = %"PRId64,
                      lkowner_utoa (&flock->l_owner),
                      get_lk_type (lk->fl_type), lk->fl_start, lk->fl_end,
                      get_lk_type (lk->user_flock.l_type),
                      lk->user_flock.l_start, lk->user_flock.l_len);

        LOCK (&lk_ctx->lock);
        {
                _fd_lk_insert_and_merge (lk_ctx, lk);
                print_lock_list (lk_ctx);
        }
        UNLOCK (&lk_ctx->lock);

        fd_lk_ctx_unref (lk_ctx);

        ret = 0;
out:
        return ret;
}


gf_boolean_t
fd_lk_ctx_empty (fd_lk_ctx_t *lk_ctx)
{
	gf_boolean_t verdict = _gf_true;

	if (!lk_ctx)
		return _gf_true;

	LOCK (&lk_ctx->lock);
	{
		verdict = list_empty (&lk_ctx->lk_list);
	}
	UNLOCK (&lk_ctx->lock);

	return verdict;
}
