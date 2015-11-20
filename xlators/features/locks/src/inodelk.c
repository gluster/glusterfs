/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"
#include "list.h"

#include "locks.h"
#include "clear.h"
#include "common.h"

void
__delete_inode_lock (pl_inode_lock_t *lock)
{
        list_del_init (&lock->list);
}

static void
__pl_inodelk_ref (pl_inode_lock_t *lock)
{
        lock->ref++;
}

void
__pl_inodelk_unref (pl_inode_lock_t *lock)
{
        lock->ref--;
        if (!lock->ref) {
                GF_FREE (lock->connection_id);
                GF_FREE (lock);
        }
}

/* Check if 2 inodelks are conflicting on type. Only 2 shared locks don't conflict */
static int
inodelk_type_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        if (l2->fl_type == F_WRLCK || l1->fl_type == F_WRLCK)
                return 1;

        return 0;
}

void
pl_print_inodelk (char *str, int size, int cmd, struct gf_flock *flock, const char *domain)
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
                cmd_str = "UNKNOWN";
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
                  "domain: %s, start=%llu, len=%llu, pid=%llu",
                  cmd_str, type_str, domain,
                  (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid);
}

/* Determine if the two inodelks overlap reach other's lock regions */
static int
inodelk_overlap (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return ((l1->fl_end >= l2->fl_start) &&
                (l2->fl_end >= l1->fl_start));
}

/* Returns true if the 2 inodelks have the same owner */
static int
same_inodelk_owner (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return (is_same_lkowner (&l1->owner, &l2->owner) &&
                (l1->client == l2->client));
}

/* Returns true if the 2 inodelks conflict with each other */
static int
inodelk_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return (inodelk_overlap (l1, l2) &&
                inodelk_type_conflict (l1, l2));
}

/*
 * Check to see if the candidate lock overlaps/conflicts with the
 * requested lock.  If so, determine how old the lock is and return
 * true if it exceeds the configured threshold, false otherwise.
 */
static inline gf_boolean_t
__stale_inodelk (xlator_t *this, pl_inode_lock_t *candidate_lock,
                pl_inode_lock_t *requested_lock, time_t *lock_age_sec)
{
        posix_locks_private_t  *priv = NULL;
        struct timeval curr;

        priv = this->private;
        gettimeofday (&curr, NULL);
        /* Question: Should we just prune them all given the
         * chance?  Or just the locks we are attempting to acquire?
         */
        if (inodelk_conflict (candidate_lock, requested_lock)) {
                *lock_age_sec = curr.tv_sec -
                        candidate_lock->granted_time.tv_sec;
                if (*lock_age_sec > priv->revocation_secs)
                        return _gf_true;
        }
        return _gf_false;
}

/* Examine any locks held on this inode and potentially revoke the lock
 * if the age exceeds revocation_secs.  We will clear _only_ those locks
 * which are granted, and then grant those locks which are blocked.
 *
 * Depending on how this patch works in the wild, we may expand this and
 * introduce a heuristic which clears blocked locks as well if they
 * are beyond a threshold.
 */
static gf_boolean_t
__inodelk_prune_stale (xlator_t *this, pl_inode_t *pinode, pl_dom_list_t *dom,
                       pl_inode_lock_t *lock)
{
        posix_locks_private_t  *priv = NULL;
        pl_inode_lock_t *tmp = NULL;
        pl_inode_lock_t *lk = NULL;
        gf_boolean_t revoke_lock = _gf_false;
        int bcount = 0;
        int gcount = 0;
        int op_errno = 0;
        clrlk_args args;
        args.opts = NULL;
        time_t lk_age_sec = 0;
        uint32_t max_blocked = 0;
        char *reason_str = NULL;

        priv = this->private;

        args.type = CLRLK_INODE;
        if (priv->revocation_clear_all == _gf_true)
                args.kind = CLRLK_ALL;
        else
                args.kind = CLRLK_GRANTED;

        if (list_empty (&dom->inodelk_list))
                goto out;

        pthread_mutex_lock (&pinode->mutex);
        list_for_each_entry_safe (lk, tmp, &dom->inodelk_list, list) {
                if (__stale_inodelk (this, lk, lock, &lk_age_sec) == _gf_true) {
                        revoke_lock = _gf_true;
                        reason_str = "age";
                        break;
                }
        }

        max_blocked = priv->revocation_max_blocked;
        if (max_blocked != 0 && revoke_lock == _gf_false) {
                list_for_each_entry_safe (lk, tmp, &dom->blocked_inodelks,
                                blocked_locks) {
                        max_blocked--;
                        if (max_blocked == 0) {
                                revoke_lock = _gf_true;
                                reason_str = "max blocked";
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&pinode->mutex);

out:
        if (revoke_lock == _gf_true) {
                clrlk_clear_inodelk (this, pinode, dom, &args, &bcount, &gcount,
                        &op_errno);
                gf_log (this->name, GF_LOG_WARNING,
                        "Lock revocation [reason: %s; gfid: %s; domain: %s; "
                        "age: %ld sec] - Inode lock revoked:  %d granted & %d "
                        "blocked locks cleared",
                        reason_str, uuid_utoa (pinode->gfid), dom->domain,
                        lk_age_sec, gcount, bcount);
        }
        return revoke_lock;
}

/* Determine if lock is grantable or not */
static pl_inode_lock_t *
__inodelk_grantable (pl_dom_list_t *dom, pl_inode_lock_t *lock)
{
        pl_inode_lock_t *l = NULL;
        pl_inode_lock_t *ret = NULL;
        if (list_empty (&dom->inodelk_list))
                goto out;
        list_for_each_entry (l, &dom->inodelk_list, list){
                if (inodelk_conflict (lock, l) &&
                    !same_inodelk_owner (lock, l)) {
                        ret = l;
                        goto out;
                }
        }
out:
        return ret;
}

static pl_inode_lock_t *
__blocked_lock_conflict (pl_dom_list_t *dom, pl_inode_lock_t *lock)
{
        pl_inode_lock_t *l   = NULL;
        pl_inode_lock_t *ret = NULL;

        if (list_empty (&dom->blocked_inodelks))
                return NULL;

        list_for_each_entry (l, &dom->blocked_inodelks, blocked_locks) {
                if (inodelk_conflict (lock, l)) {
                        ret = l;
                        goto out;
                }
        }

out:
        return ret;
}

static int
__owner_has_lock (pl_dom_list_t *dom, pl_inode_lock_t *newlock)
{
        pl_inode_lock_t *lock = NULL;

        list_for_each_entry (lock, &dom->inodelk_list, list) {
                if (same_inodelk_owner (lock, newlock))
                        return 1;
        }

        list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {
                if (same_inodelk_owner (lock, newlock))
                        return 1;
        }

        return 0;
}


/* Determines if lock can be granted and adds the lock. If the lock
 * is blocking, adds it to the blocked_inodelks list of the domain.
 */
static int
__lock_inodelk (xlator_t *this, pl_inode_t *pl_inode, pl_inode_lock_t *lock,
                int can_block,  pl_dom_list_t *dom)
{
        pl_inode_lock_t *conf = NULL;
        int ret = -EINVAL;

        conf = __inodelk_grantable (dom, lock);
        if (conf) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }

        /* To prevent blocked locks starvation, check if there are any blocked
         * locks thay may conflict with this lock. If there is then don't grant
         * the lock. BUT grant the lock if the owner already has lock to allow
         * nested locks.
         * Example:
         * SHD from Machine1 takes (gfid, 0-infinity) and is granted.
         * SHD from machine2 takes (gfid, 0-infinity) and is blocked.
         * When SHD from Machine1 takes (gfid, 0-128KB) it
         * needs to be granted, without which the earlier lock on 0-infinity
         * will not be unlocked by SHD from Machine1.
         * TODO: Find why 'owner_has_lock' is checked even for blocked locks.
         */
        if (__blocked_lock_conflict (dom, lock) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_DEBUG,
                        "Lock is grantable, but blocking to prevent starvation");
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }
        __pl_inodelk_ref (lock);
        gettimeofday (&lock->granted_time, NULL);
        list_add (&lock->list, &dom->inodelk_list);

        ret = 0;

out:
        return ret;
}

/* Return true if the two inodelks have exactly same lock boundaries */
static int
inodelks_equal (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        if ((l1->fl_start == l2->fl_start) &&
            (l1->fl_end == l2->fl_end))
                return 1;

        return 0;
}


static pl_inode_lock_t *
find_matching_inodelk (pl_inode_lock_t *lock, pl_dom_list_t *dom)
{
        pl_inode_lock_t *l = NULL;
        list_for_each_entry (l, &dom->inodelk_list, list) {
                if (inodelks_equal (l, lock) &&
                    same_inodelk_owner (l, lock))
                        return l;
        }
        return NULL;
}

/* Set F_UNLCK removes a lock which has the exact same lock boundaries
 * as the UNLCK lock specifies. If such a lock is not found, returns invalid
 */
static pl_inode_lock_t *
__inode_unlock_lock (xlator_t *this, pl_inode_lock_t *lock, pl_dom_list_t *dom)
{

        pl_inode_lock_t *conf = NULL;

        conf = find_matching_inodelk (lock, dom);
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        " Matching lock not found for unlock %llu-%llu, by %s "
                        "on %p", (unsigned long long)lock->fl_start,
                        (unsigned long long)lock->fl_end,
                        lkowner_utoa (&lock->owner), lock->client);
                goto out;
        }
        __delete_inode_lock (conf);
        gf_log (this->name, GF_LOG_DEBUG,
                " Matching lock found for unlock %llu-%llu, by %s on %p",
                (unsigned long long)lock->fl_start,
                (unsigned long long)lock->fl_end, lkowner_utoa (&lock->owner),
                lock->client);

out:
        return conf;
}


static void
__grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode,
                             struct list_head *granted, pl_dom_list_t *dom)
{
        int              bl_ret = 0;
        pl_inode_lock_t *bl = NULL;
        pl_inode_lock_t *tmp = NULL;

        struct list_head blocked_list;

        INIT_LIST_HEAD (&blocked_list);
        list_splice_init (&dom->blocked_inodelks, &blocked_list);

        list_for_each_entry_safe (bl, tmp, &blocked_list, blocked_locks) {

                list_del_init (&bl->blocked_locks);

                bl_ret = __lock_inodelk (this, pl_inode, bl, 1, dom);

                if (bl_ret == 0) {
                        list_add (&bl->blocked_locks, granted);
                }
        }
        return;
}

/* Grant all inodelks blocked on a lock */
void
grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode,
                           pl_dom_list_t *dom)
{
        struct list_head granted;
        pl_inode_lock_t *lock;
        pl_inode_lock_t *tmp;

        INIT_LIST_HEAD (&granted);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_inode_locks (this, pl_inode, &granted, dom);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => Granted",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);

                pl_trace_out (this, lock->frame, NULL, NULL, F_SETLKW,
                              &lock->user_flock, 0, 0, lock->volume);

                STACK_UNWIND_STRICT (inodelk, lock->frame, 0, 0, NULL);
		lock->frame = NULL;
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
                        list_del_init (&lock->blocked_locks);
                        __pl_inodelk_unref (lock);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);
}


static void
pl_inodelk_log_cleanup (pl_inode_lock_t *lock)
{
	pl_inode_t *pl_inode = NULL;

	pl_inode = lock->pl_inode;

        gf_log (THIS->name, GF_LOG_WARNING, "releasing lock on %s held by "
                "{client=%p, pid=%"PRId64" lk-owner=%s}",
                uuid_utoa (pl_inode->gfid), lock->client,
                (uint64_t) lock->client_pid, lkowner_utoa (&lock->owner));
}


/* Release all inodelks from this client */
int
pl_inodelk_client_cleanup (xlator_t *this, pl_ctx_t *ctx)
{
        pl_inode_lock_t *tmp = NULL;
        pl_inode_lock_t *l = NULL;
	pl_dom_list_t *dom = NULL;
        pl_inode_t *pl_inode = NULL;

        struct list_head released;
        struct list_head unwind;

        INIT_LIST_HEAD (&released);
        INIT_LIST_HEAD (&unwind);

	pthread_mutex_lock (&ctx->lock);
        {
                list_for_each_entry_safe (l, tmp, &ctx->inodelk_lockers,
					  client_list) {
			pl_inodelk_log_cleanup (l);

			pl_inode = l->pl_inode;

			pthread_mutex_lock (&pl_inode->mutex);
			{
                        /* If the inodelk object is part of granted list but not
                         * blocked list, then perform the following actions:
                         * i.   delete the object from granted list;
                         * ii.  grant other locks (from other clients) that may
                         *      have been blocked on this inodelk; and
                         * iii. unref the object.
                         *
                         * If the inodelk object (L1) is part of both granted
                         * and blocked lists, then this means that a parallel
                         * unlock on another inodelk (L2 say) may have 'granted'
                         * L1 and added it to 'granted' list in
                         * __grant_blocked_node_locks() (although using the
                         * 'blocked_locks' member). In that case, the cleanup
                         * codepath must try and grant other overlapping
                         * blocked inodelks from other clients, now that L1 is
                         * out of their way and then unref L1 in the end, and
                         * leave it to the other thread (the one executing
                         * unlock codepath) to unwind L1's frame, delete it from
                         * blocked_locks list, and perform the last unref on L1.
                         *
                         * If the inodelk object (L1) is part of blocked list
                         * only, the cleanup code path must:
                         * i.   delete it from the blocked_locks list inside
                         *      this critical section,
                         * ii.  unwind its frame with EAGAIN,
                         * iii. try and grant blocked inode locks from other
                         *      clients that were otherwise grantable, but just
                         *      got blocked to avoid leaving L1 to starve
                         *      forever.
                         * iv.  unref the object.
                         */
                                list_del_init (&l->client_list);

                                if (!list_empty (&l->list)) {
                                        __delete_inode_lock (l);
                                        list_add_tail (&l->client_list,
                                                       &released);
                                } else {
                                        list_del_init(&l->blocked_locks);
                                        list_add_tail (&l->client_list,
                                                       &unwind);
                                }
                        }
			pthread_mutex_unlock (&pl_inode->mutex);
                }
	}
        pthread_mutex_unlock (&ctx->lock);

        list_for_each_entry_safe (l, tmp, &unwind, client_list) {
                list_del_init (&l->client_list);

                if (l->frame)
			STACK_UNWIND_STRICT (inodelk, l->frame, -1, EAGAIN,
					     NULL);
                list_add_tail (&l->client_list, &released);

        }

        list_for_each_entry_safe (l, tmp, &released, client_list) {
                list_del_init (&l->client_list);

		pl_inode = l->pl_inode;

		dom = get_domain (pl_inode, l->volume);

		grant_blocked_inode_locks (this, pl_inode, dom);

		pthread_mutex_lock (&pl_inode->mutex);
		{
			__pl_inodelk_unref (l);
		}
		pthread_mutex_unlock (&pl_inode->mutex);
                inode_unref (pl_inode->inode);
        }

        return 0;
}


static int
pl_inode_setlk (xlator_t *this, pl_ctx_t *ctx, pl_inode_t *pl_inode,
		pl_inode_lock_t *lock, int can_block, pl_dom_list_t *dom,
                inode_t *inode)
{
        posix_locks_private_t  *priv = NULL;
        int               ret              = -EINVAL;
        pl_inode_lock_t  *retlock          =  NULL;
        gf_boolean_t      unref            =  _gf_true;
        gf_boolean_t      need_inode_unref =  _gf_false;
        short             fl_type;

	lock->pl_inode = pl_inode;
        fl_type = lock->fl_type;

        priv = this->private;

        /* Ideally, AFTER a successful lock (both blocking and non-blocking) or
         * an unsuccessful blocking lock operation, the inode needs to be ref'd.
         *
         * But doing so might give room to a race where the lock-requesting
         * client could send a DISCONNECT just before this thread refs the inode
         * after the locking is done, and the epoll thread could unref the inode
         * in cleanup which means the inode's refcount would come down to 0, and
         * the call to pl_forget() at this point destroys @pl_inode. Now when
         * the io-thread executing this function tries to access pl_inode,
         * it could crash on account of illegal memory access.
         *
         * To get around this problem, the inode is ref'd once even before
         * adding the lock into client_list as a precautionary measure.
         * This way even if there are DISCONNECTs, there will always be 1 extra
         * ref on the inode, so @pl_inode is still alive until after the
         * current stack unwinds.
         */
        pl_inode->inode = inode_ref (inode);

        if (priv->revocation_secs != 0) {
                if (lock->fl_type != F_UNLCK) {
                        __inodelk_prune_stale (this, pl_inode, dom, lock);
                } else if (priv->monkey_unlocking == _gf_true) {
                        if (pl_does_monkey_want_stuck_lock ()) {
                                pthread_mutex_lock (&pl_inode->mutex);
                                {
                                        __pl_inodelk_unref (lock);
                                }
                                pthread_mutex_unlock (&pl_inode->mutex);
                                inode_unref (pl_inode->inode);
                                gf_log (this->name, GF_LOG_WARNING,
                                    "MONKEY LOCKING (forcing stuck lock)!");
                                return 0;
                        }
                }
        }

	if (ctx)
		pthread_mutex_lock (&ctx->lock);
        pthread_mutex_lock (&pl_inode->mutex);
        {
                if (lock->fl_type != F_UNLCK) {
                        ret = __lock_inodelk (this, pl_inode, lock, can_block, dom);
                        if (ret == 0) {
				lock->frame = NULL;
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => OK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->fl_start,
                                        lock->fl_end);
                        } else if (ret == -EAGAIN) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => NOK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->user_flock.l_start,
                                        lock->user_flock.l_len);
                                if (can_block)
                                        unref = _gf_false;
                                /* For all but the case where a non-blocking
                                 * lock attempt fails, the extra ref taken at
                                 * the start of this function must be negated.
                                 */
                                else
                                        need_inode_unref = _gf_true;
                        }

			if (ctx && (!ret || can_block))
				list_add_tail (&lock->client_list,
					       &ctx->inodelk_lockers);
                } else {
                        /* Irrespective of whether unlock succeeds or not,
                         * the extra inode ref that was done at the start of
                         * this function must be negated. Towards this,
                         * @need_inode_unref flag is set unconditionally here.
                         */
                        need_inode_unref = _gf_true;
                        retlock = __inode_unlock_lock (this, lock, dom);
                        if (!retlock) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Bad Unlock issued on Inode lock");
                                ret = -EINVAL;
                                goto out;
                        }
			list_del_init (&retlock->client_list);
			__pl_inodelk_unref (retlock);

                        ret = 0;
                }
out:
		if (unref)
			__pl_inodelk_unref (lock);
        }
        pthread_mutex_unlock (&pl_inode->mutex);
	if (ctx)
		pthread_mutex_unlock (&ctx->lock);

        if (need_inode_unref)
                inode_unref (pl_inode->inode);

        /* The following (extra) unref corresponds to the ref that
         * was done at the time the lock was granted.
         */
        if ((fl_type == F_UNLCK) && (ret == 0)) {
                inode_unref (pl_inode->inode);
                grant_blocked_inode_locks (this, pl_inode, dom);
        }

        return ret;
}

/* Create a new inode_lock_t */
pl_inode_lock_t *
new_inode_lock (struct gf_flock *flock, client_t *client, pid_t client_pid,
                call_frame_t *frame, xlator_t *this, const char *volume,
                char *conn_id)

{
        pl_inode_lock_t *lock = NULL;

        lock = GF_CALLOC (1, sizeof (*lock),
                          gf_locks_mt_pl_inode_lock_t);
        if (!lock) {
                return NULL;
        }

        lock->fl_start = flock->l_start;
        lock->fl_type  = flock->l_type;

        if (flock->l_len == 0)
                lock->fl_end = LLONG_MAX;
        else
                lock->fl_end = flock->l_start + flock->l_len - 1;

        lock->client     = client;
        lock->client_pid = client_pid;
        lock->volume     = volume;
        lock->owner      = frame->root->lk_owner;
        lock->frame      = frame;
        lock->this       = this;

        if (conn_id) {
                lock->connection_id = gf_strdup (conn_id);
        }

        INIT_LIST_HEAD (&lock->list);
        INIT_LIST_HEAD (&lock->blocked_locks);
	INIT_LIST_HEAD (&lock->client_list);
        __pl_inodelk_ref (lock);

        return lock;
}

int32_t
_pl_convert_volume (const char *volume, char **res)
{
        char    *mdata_vol = NULL;
        int     ret = 0;

        mdata_vol = strrchr (volume, ':');
        //if the volume already ends with :metadata don't bother
        if (mdata_vol && (strcmp (mdata_vol, ":metadata") == 0))
                return 0;

        ret = gf_asprintf (res, "%s:metadata", volume);
        if (ret <= 0)
                return ENOMEM;
        return 0;
}

int32_t
_pl_convert_volume_for_special_range (struct gf_flock *flock,
                                      const char *volume, char **res)
{
        int32_t     ret = 0;

        if ((flock->l_start == LLONG_MAX -1) &&
            (flock->l_len == 0)) {
                ret = _pl_convert_volume (volume, res);
        }

        return ret;
}

/* Common inodelk code called from pl_inodelk and pl_finodelk */
int
pl_common_inodelk (call_frame_t *frame, xlator_t *this,
                   const char *volume, inode_t *inode, int32_t cmd,
                   struct gf_flock *flock, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        int32_t           op_ret     = -1;
        int32_t           op_errno   = 0;
        int               ret        = -1;
        GF_UNUSED int     dict_ret   = -1;
        int               can_block  = 0;
        pl_inode_t *      pinode     = NULL;
        pl_inode_lock_t * reqlock    = NULL;
        pl_dom_list_t *   dom        = NULL;
        char             *res        = NULL;
        char             *res1       = NULL;
        char             *conn_id    = NULL;
        pl_ctx_t         *ctx        = NULL;

        if (xdata)
                dict_ret = dict_get_str (xdata, "connection-id", &conn_id);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (inode, unwind);
        VALIDATE_OR_GOTO (flock, unwind);

        if ((flock->l_start < 0) || (flock->l_len < 0)) {
                op_errno = EINVAL;
                goto unwind;
        }

        op_errno = _pl_convert_volume_for_special_range (flock, volume, &res);
        if (op_errno)
                goto unwind;
        if (res)
                volume = res;

        pl_trace_in (this, frame, fd, loc, cmd, flock, volume);

	if (frame->root->client) {
		ctx = pl_ctx_get (frame->root->client, this);
		if (!ctx) {
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_INFO, "pl_ctx_get() failed");
			goto unwind;
		}
	}

        pinode = pl_inode_get (this, inode);
        if (!pinode) {
                op_errno = ENOMEM;
                goto unwind;
        }

        dom = get_domain (pinode, volume);
        if (!dom) {
                op_errno = ENOMEM;
                goto unwind;
        }

        reqlock = new_inode_lock (flock, frame->root->client, frame->root->pid,
                                  frame, this, dom->domain, conn_id);

        if (!reqlock) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }


        switch (cmd) {
        case F_SETLKW:
                can_block = 1;

                /* fall through */

        case F_SETLK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct gf_flock));
                ret = pl_inode_setlk (this, ctx, pinode, reqlock, can_block,
				      dom, inode);

                if (ret < 0) {
                        if ((can_block) && (F_UNLCK != flock->l_type)) {
                                pl_trace_block (this, frame, fd, loc,
                                                cmd, flock, volume);
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_TRACE, "returning EAGAIN");
                        op_errno = -ret;
                        goto unwind;
                }
                break;

        default:
                op_errno = ENOTSUP;
                gf_log (this->name, GF_LOG_DEBUG,
                        "Lock command F_GETLK not supported for [f]inodelk "
                        "(cmd=%d)",
                        cmd);
                goto unwind;
        }

        op_ret = 0;

unwind:
        if (flock != NULL)
                pl_trace_out (this, frame, fd, loc, cmd, flock, op_ret,
                              op_errno, volume);

        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, NULL);
out:
        GF_FREE (res);
        GF_FREE (res1);
        return 0;
}

int
pl_inodelk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock,
            dict_t *xdata)
{
        pl_common_inodelk (frame, this, volume, loc->inode, cmd, flock,
                           loc, NULL, xdata);

        return 0;
}

int
pl_finodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock,
             dict_t *xdata)
{
        pl_common_inodelk (frame, this, volume, fd->inode, cmd, flock,
                           NULL, fd, xdata);

        return 0;

}

static int32_t
__get_inodelk_dom_count (pl_dom_list_t *dom)
{
        pl_inode_lock_t     *lock   = NULL;
        int32_t             count   = 0;

        list_for_each_entry (lock, &dom->inodelk_list, list) {
                count++;
        }
        list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {
                count++;
        }
        return count;
}

/* Returns the no. of locks (blocked/granted) held on a given domain name
 * If @domname is NULL, returns the no. of locks in all the domains present.
 * If @domname is non-NULL and non-existent, returns 0 */
int32_t
__get_inodelk_count (xlator_t *this, pl_inode_t *pl_inode, char *domname)
{
        int32_t            count  = 0;
        pl_dom_list_t     *dom    = NULL;

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                if (domname) {
                        if (strcmp (domname, dom->domain) == 0) {
                                count = __get_inodelk_dom_count (dom);
                                goto out;
                        }

                } else {
                    /* Counting locks from all domains */
                        count += __get_inodelk_dom_count (dom);

                }
        }

out:
        return count;
}

int32_t
get_inodelk_count (xlator_t *this, inode_t *inode, char *domname)
{
        pl_inode_t   *pl_inode = NULL;
        uint64_t      tmp_pl_inode = 0;
        int           ret      = 0;
        int32_t       count    = 0;

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);
        if (ret != 0) {
                goto out;
        }

        pl_inode = (pl_inode_t *)(long) tmp_pl_inode;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                count = __get_inodelk_count (this, pl_inode, domname);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}
