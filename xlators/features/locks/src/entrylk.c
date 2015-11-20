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
__pl_entrylk_unref (pl_entry_lock_t *lock)
{
        lock->ref--;
        if (!lock->ref) {
		GF_FREE ((char *)lock->basename);
                GF_FREE (lock->connection_id);
                GF_FREE (lock);
        }
}


static void
__pl_entrylk_ref (pl_entry_lock_t *lock)
{
        lock->ref++;
}


static pl_entry_lock_t *
new_entrylk_lock (pl_inode_t *pinode, const char *basename, entrylk_type type,
		  const char *domain, call_frame_t *frame, char *conn_id)
{
        pl_entry_lock_t *newlock = NULL;

        newlock = GF_CALLOC (1, sizeof (pl_entry_lock_t),
                             gf_locks_mt_pl_entry_lock_t);
        if (!newlock) {
                goto out;
        }

        newlock->basename   = basename ? gf_strdup (basename) : NULL;
        newlock->type       = type;
        newlock->client     = frame->root->client;
        newlock->client_pid = frame->root->pid;
        newlock->volume     = domain;
        newlock->owner      = frame->root->lk_owner;
	newlock->frame      = frame;
        newlock->this       = frame->this;

        if (conn_id) {
                newlock->connection_id = gf_strdup (conn_id);
        }

        INIT_LIST_HEAD (&newlock->domain_list);
        INIT_LIST_HEAD (&newlock->blocked_locks);
	INIT_LIST_HEAD (&newlock->client_list);

	__pl_entrylk_ref (newlock);
out:
        return newlock;
}


/**
 * all_names - does a basename represent all names?
 * @basename: name to check
 */

#define all_names(basename) ((basename == NULL) ? 1 : 0)

/**
 * names_conflict - do two names conflict?
 * @n1: name
 * @n2: name
 */

static int
names_conflict (const char *n1, const char *n2)
{
        return all_names (n1) || all_names (n2) || !strcmp (n1, n2);
}


static int
__same_entrylk_owner (pl_entry_lock_t *l1, pl_entry_lock_t *l2)
{
        return (is_same_lkowner (&l1->owner, &l2->owner) &&
                (l1->client  == l2->client));
}

/* Just as in inodelk, allow conflicting name locks from same (lk_owner, conn)*/
static int
__conflicting_entrylks (pl_entry_lock_t *l1, pl_entry_lock_t *l2)
{
        if (names_conflict (l1->basename, l2->basename)
            && !__same_entrylk_owner (l1, l2))
                return 1;

        return 0;
}

/* See comments in inodelk.c for details */
static inline gf_boolean_t
__stale_entrylk (xlator_t *this, pl_entry_lock_t *candidate_lock,
                pl_entry_lock_t *requested_lock, time_t *lock_age_sec)
{
        posix_locks_private_t  *priv = NULL;
        struct timeval curr;
        gettimeofday (&curr, NULL);

        priv = this->private;

        /* Question: Should we just prune them all given the
         * chance?  Or just the locks we are attempting to acquire?
         */
        if (names_conflict (candidate_lock->basename,
                        requested_lock->basename)) {
                *lock_age_sec = curr.tv_sec -
                        candidate_lock->granted_time.tv_sec;
                if (*lock_age_sec > priv->revocation_secs)
                        return _gf_true;
        }
        return _gf_false;
}

/* See comments in inodelk.c for details */
static gf_boolean_t
__entrylk_prune_stale (xlator_t *this, pl_inode_t *pinode, pl_dom_list_t *dom,
                 pl_entry_lock_t *lock)
{
        posix_locks_private_t  *priv = NULL;
        pl_entry_lock_t *tmp = NULL;
        pl_entry_lock_t *lk = NULL;
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
        args.type = CLRLK_ENTRY;
        if (priv->revocation_clear_all == _gf_true)
                args.kind = CLRLK_ALL;
        else
                args.kind = CLRLK_GRANTED;


        if (list_empty (&dom->entrylk_list))
                goto out;

        pthread_mutex_lock (&pinode->mutex);
        lock->pinode = pinode;
        list_for_each_entry_safe (lk, tmp, &dom->entrylk_list, domain_list) {
                if (__stale_entrylk (this, lk, lock, &lk_age_sec) == _gf_true) {
                        revoke_lock = _gf_true;
                        reason_str = "age";
                        break;
                }
        }
        max_blocked = priv->revocation_max_blocked;
        if (max_blocked != 0 && revoke_lock == _gf_false) {
                list_for_each_entry_safe (lk, tmp, &dom->blocked_entrylks,
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
                clrlk_clear_entrylk (this, pinode, dom, &args, &bcount, &gcount,
                    &op_errno);
                gf_log (this->name, GF_LOG_WARNING,
                        "Lock revocation [reason: %s; gfid: %s; domain: %s; "
                        "age: %ld sec] - Entry lock revoked:  %d granted & %d "
                        "blocked locks cleared", reason_str,
                        uuid_utoa (pinode->gfid), dom->domain, lk_age_sec,
                        gcount, bcount);
        }

        return revoke_lock;
}

/**
 * entrylk_grantable - is this lock grantable?
 * @inode: inode in which to look
 * @basename: name we're trying to lock
 * @type: type of lock
 */
static pl_entry_lock_t *
__entrylk_grantable (pl_dom_list_t *dom, pl_entry_lock_t *lock)
{
        pl_entry_lock_t *tmp = NULL;

        if (list_empty (&dom->entrylk_list))
                return NULL;

        list_for_each_entry (tmp, &dom->entrylk_list, domain_list) {
                if (__conflicting_entrylks (tmp, lock))
                        return tmp;
        }

        return NULL;
}

static pl_entry_lock_t *
__blocked_entrylk_conflict (pl_dom_list_t *dom, pl_entry_lock_t *lock)
{
        pl_entry_lock_t *tmp = NULL;

        if (list_empty (&dom->blocked_entrylks))
                return NULL;

        list_for_each_entry (tmp, &dom->blocked_entrylks, blocked_locks) {
                if (names_conflict (tmp->basename, lock->basename))
                        return lock;
        }

        return NULL;
}

static int
__owner_has_lock (pl_dom_list_t *dom, pl_entry_lock_t *newlock)
{
        pl_entry_lock_t *lock = NULL;

        list_for_each_entry (lock, &dom->entrylk_list, domain_list) {
                if (__same_entrylk_owner (lock, newlock))
                        return 1;
        }

        list_for_each_entry (lock, &dom->blocked_entrylks, blocked_locks) {
                if (__same_entrylk_owner (lock, newlock))
                        return 1;
        }

        return 0;
}

static int
names_equal (const char *n1, const char *n2)
{
        return (n1 == NULL && n2 == NULL) || (n1 && n2 && !strcmp (n1, n2));
}

void
pl_print_entrylk (char *str, int size, entrylk_cmd cmd, entrylk_type type,
                  const char *basename, const char *domain)
{
        char *cmd_str = NULL;
        char *type_str = NULL;

        switch (cmd) {
        case ENTRYLK_LOCK:
                cmd_str = "LOCK";
                break;

        case ENTRYLK_LOCK_NB:
                cmd_str = "LOCK_NB";
                break;

        case ENTRYLK_UNLOCK:
                cmd_str = "UNLOCK";
                break;

        default:
                cmd_str = "UNKNOWN";
                break;
        }

        switch (type) {
        case ENTRYLK_RDLCK:
                type_str = "READ";
                break;
        case ENTRYLK_WRLCK:
                type_str = "WRITE";
                break;
        default:
                type_str = "UNKNOWN";
                break;
        }

        snprintf (str, size, "lock=ENTRYLK, cmd=%s, type=%s, basename=%s, domain: %s",
                  cmd_str, type_str, basename, domain);
}


void
entrylk_trace_in (xlator_t *this, call_frame_t *frame, const char *domain,
                  fd_t *fd, loc_t *loc, const char *basename,
                  entrylk_cmd cmd, entrylk_type type)
{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_entrylk[256];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        pl_print_entrylk (pl_entrylk, 256, cmd, type, basename, domain);

        gf_log (this->name, GF_LOG_INFO,
                "[REQUEST] Locker = {%s} Lockee = {%s} Lock = {%s}",
                pl_locker, pl_lockee, pl_entrylk);
}


void
entrylk_trace_out (xlator_t *this, call_frame_t *frame, const char *domain,
                   fd_t *fd, loc_t *loc, const char *basename,
                   entrylk_cmd cmd, entrylk_type type, int op_ret, int op_errno)
{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_entrylk[256];
        char                    verdict[32];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        pl_print_entrylk (pl_entrylk, 256, cmd, type, basename, domain);
        pl_print_verdict (verdict, 32, op_ret, op_errno);

        gf_log (this->name, GF_LOG_INFO,
                "[%s] Locker = {%s} Lockee = {%s} Lock = {%s}",
                verdict, pl_locker, pl_lockee, pl_entrylk);
}


void
entrylk_trace_block (xlator_t *this, call_frame_t *frame, const char *volume,
                     fd_t *fd, loc_t *loc, const char *basename,
                     entrylk_cmd cmd, entrylk_type type)

{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_entrylk[256];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        pl_print_entrylk (pl_entrylk, 256, cmd, type, basename, volume);

        gf_log (this->name, GF_LOG_INFO,
                "[BLOCKED] Locker = {%s} Lockee = {%s} Lock = {%s}",
                pl_locker, pl_lockee, pl_entrylk);
}

/**
 * __find_most_matching_lock - find the lock struct which most matches in order of:
 *                           lock on the exact basename ||
 *                           an all_names lock
 *
 *
 * @inode: inode in which to look
 * @basename: name to search for
 */

static pl_entry_lock_t *
__find_most_matching_lock (pl_dom_list_t *dom, const char *basename)
{
        pl_entry_lock_t *lock;
        pl_entry_lock_t *all = NULL;
        pl_entry_lock_t *exact = NULL;

        if (list_empty (&dom->entrylk_list))
                return NULL;

        list_for_each_entry (lock, &dom->entrylk_list, domain_list) {
                if (all_names (lock->basename))
                        all = lock;
                else if (names_equal (lock->basename, basename))
                        exact = lock;
        }

        return (exact ? exact : all);
}

static pl_entry_lock_t*
__find_matching_lock (pl_dom_list_t *dom, pl_entry_lock_t *lock)
{
        pl_entry_lock_t *tmp = NULL;

        list_for_each_entry (tmp, &dom->entrylk_list, domain_list) {
                if (names_equal (lock->basename, tmp->basename)
                    && __same_entrylk_owner (lock, tmp)
                    && (lock->type == tmp->type))
                        return tmp;
        }
        return NULL;
}

/**
 * __lock_entrylk - lock a name in a directory
 * @inode: inode for the directory in which to lock
 * @basename: name of the entry to lock
 *            if null, lock the entire directory
 *
 * the entire directory being locked is represented as: a single
 * pl_entry_lock_t present in the entrylk_locks list with its
 * basename = NULL
 */

int
__lock_entrylk (xlator_t *this, pl_inode_t *pinode, pl_entry_lock_t *lock,
		int nonblock, pl_dom_list_t *dom)
{
        pl_entry_lock_t *conf = NULL;
        int              ret  = -EAGAIN;

        conf = __entrylk_grantable (dom, lock);
        if (conf) {
                ret = -EAGAIN;
                if (nonblock)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_entrylks);

                gf_log (this->name, GF_LOG_TRACE,
                        "Blocking lock: {pinode=%p, basename=%s}",
                        pinode, lock->basename);

                goto out;
        }

        /* To prevent blocked locks starvation, check if there are any blocked
         * locks thay may conflict with this lock. If there is then don't grant
         * the lock. BUT grant the lock if the owner already has lock to allow
         * nested locks.
         * Example: SHD from Machine1 takes (gfid, basename=257-length-name)
         * and is granted.
         * SHD from machine2 takes (gfid, basename=NULL) and is blocked.
         * When SHD from Machine1 takes (gfid, basename=NULL) it needs to be
         * granted, without which self-heal can't progress.
         * TODO: Find why 'owner_has_lock' is checked even for blocked locks.
         */
        if (__blocked_entrylk_conflict (dom, lock) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (nonblock)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_entrylks);

                gf_log (this->name, GF_LOG_DEBUG,
                        "Lock is grantable, but blocking to prevent starvation");
                gf_log (this->name, GF_LOG_TRACE,
                        "Blocking lock: {pinode=%p, basename=%s}",
                        pinode, lock->basename);

                goto out;
        }

        __pl_entrylk_ref (lock);
        gettimeofday (&lock->granted_time, NULL);
        list_add (&lock->domain_list, &dom->entrylk_list);

        ret = 0;
out:
        return ret;
}

/**
 * __unlock_entrylk - unlock a name in a directory
 * @inode: inode for the directory to unlock in
 * @basename: name of the entry to unlock
 *            if null, unlock the entire directory
 */

pl_entry_lock_t *
__unlock_entrylk (pl_dom_list_t *dom, pl_entry_lock_t *lock)
{
        pl_entry_lock_t *ret_lock = NULL;

        ret_lock = __find_matching_lock (dom, lock);

        if (ret_lock) {
                list_del_init (&ret_lock->domain_list);
        } else {
                gf_log ("locks", GF_LOG_ERROR, "unlock on %s "
                        "(type=ENTRYLK_WRLCK) attempted but no matching lock "
                        "found", lock->basename);
        }

        return ret_lock;
}

int32_t
check_entrylk_on_basename (xlator_t *this, inode_t *parent, char *basename)
{
        int32_t         entrylk = 0;
        pl_inode_t      *pinode = 0;
        pl_dom_list_t   *dom = NULL;
        pl_entry_lock_t *conf       = NULL;

        pinode = pl_inode_get (this, parent);
        if (!pinode)
                goto out;
        pthread_mutex_lock (&pinode->mutex);
        {
                list_for_each_entry (dom, &pinode->dom_list, inode_list) {
                        conf = __find_most_matching_lock (dom, basename);
                        if (conf && conf->basename) {
                                entrylk = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&pinode->mutex);

out:
        return entrylk;
}

void
__grant_blocked_entry_locks (xlator_t *this, pl_inode_t *pl_inode,
                             pl_dom_list_t *dom, struct list_head *granted)
{
        int              bl_ret = 0;
        pl_entry_lock_t *bl   = NULL;
        pl_entry_lock_t *tmp  = NULL;

        struct list_head blocked_list;

        INIT_LIST_HEAD (&blocked_list);
        list_splice_init (&dom->blocked_entrylks, &blocked_list);

        list_for_each_entry_safe (bl, tmp, &blocked_list, blocked_locks) {

                list_del_init (&bl->blocked_locks);

                bl_ret = __lock_entrylk (bl->this, pl_inode, bl, 0, dom);

                if (bl_ret == 0) {
                        list_add (&bl->blocked_locks, granted);
                }
        }
        return;
}

/* Grants locks if possible which are blocked on a lock */
void
grant_blocked_entry_locks (xlator_t *this, pl_inode_t *pl_inode,
			   pl_dom_list_t *dom)
{
        struct list_head  granted_list;
        pl_entry_lock_t  *tmp = NULL;
        pl_entry_lock_t  *lock = NULL;

        INIT_LIST_HEAD (&granted_list);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_entry_locks (this, pl_inode, dom,
                                             &granted_list);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted_list, blocked_locks) {
                entrylk_trace_out (this, lock->frame, NULL, NULL, NULL,
                                   lock->basename, ENTRYLK_LOCK, lock->type,
                                   0, 0);

                STACK_UNWIND_STRICT (entrylk, lock->frame, 0, 0, NULL);
		lock->frame = NULL;
	}

        pthread_mutex_lock (&pl_inode->mutex);
        {
		list_for_each_entry_safe (lock, tmp, &granted_list, blocked_locks) {
			list_del_init (&lock->blocked_locks);
			__pl_entrylk_unref (lock);
		}
	}
        pthread_mutex_unlock (&pl_inode->mutex);

        return;
}


/* Common entrylk code called by pl_entrylk and pl_fentrylk */
int
pl_common_entrylk (call_frame_t *frame, xlator_t *this,
                   const char *volume, inode_t *inode, const char *basename,
                   entrylk_cmd cmd, entrylk_type type, loc_t *loc, fd_t *fd,
                   dict_t *xdata)

{
        int32_t          op_ret           = -1;
        int32_t          op_errno         =  0;
        int              ret              = -1;
        char             unwind           =  1;
        GF_UNUSED int    dict_ret         = -1;
        pl_inode_t      *pinode           =  NULL;
        pl_entry_lock_t *reqlock          =  NULL;
        pl_entry_lock_t *unlocked         =  NULL;
        pl_dom_list_t   *dom              =  NULL;
        char            *conn_id          =  NULL;
        pl_ctx_t        *ctx              =  NULL;
	int              nonblock         =  0;
        gf_boolean_t     need_inode_unref =  _gf_false;
        posix_locks_private_t  *priv = NULL;

        priv = this->private;

        if (xdata)
                dict_ret = dict_get_str (xdata, "connection-id", &conn_id);

        pinode = pl_inode_get (this, inode);
        if (!pinode) {
                op_errno = ENOMEM;
                goto out;
        }

	if (frame->root->client) {
		ctx = pl_ctx_get (frame->root->client, this);
		if (!ctx) {
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_INFO, "pl_ctx_get() failed");
			goto unwind;
		}
	}

        dom = get_domain (pinode, volume);
        if (!dom){
                op_errno = ENOMEM;
                goto out;
        }

        entrylk_trace_in (this, frame, volume, fd, loc, basename, cmd, type);

        reqlock = new_entrylk_lock (pinode, basename, type, dom->domain, frame,
				    conn_id);
        if (!reqlock) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        /* Ideally, AFTER a successful lock (both blocking and non-blocking) or
         * an unsuccessful blocking lock operation, the inode needs to be ref'd.
         *
         * But doing so might give room to a race where the lock-requesting
         * client could send a DISCONNECT just before this thread refs the inode
         * after the locking is done, and the epoll thread could unref the inode
         * in cleanup which means the inode's refcount would come down to 0, and
         * the call to pl_forget() at this point destroys @pinode. Now when
         * the io-thread executing this function tries to access pinode,
         * it could crash on account of illegal memory access.
         *
         * To get around this problem, the inode is ref'd once even before
         * adding the lock into client_list as a precautionary measure.
         * This way even if there are DISCONNECTs, there will always be 1 extra
         * ref on the inode, so @pinode is still alive until after the
         * current stack unwinds.
         */
        pinode->inode = inode_ref (inode);
        if (priv->revocation_secs != 0) {
                if (cmd != ENTRYLK_UNLOCK) {
                        __entrylk_prune_stale (this, pinode, dom, reqlock);
                } else if (priv->monkey_unlocking == _gf_true) {
                        if (pl_does_monkey_want_stuck_lock ()) {
                                gf_log (this->name, GF_LOG_WARNING,
                                    "MONKEY LOCKING (forcing stuck lock)!");
                                op_ret = 0;
                                need_inode_unref = _gf_true;
                                pthread_mutex_lock (&pinode->mutex);
                                {
                                        __pl_entrylk_unref (reqlock);
                                }
                                pthread_mutex_unlock (&pinode->mutex);
                                goto out;
                        }
                }
        }

        switch (cmd) {
        case ENTRYLK_LOCK_NB:
		nonblock = 1;
		/* fall through */
	case ENTRYLK_LOCK:
		if (ctx)
			pthread_mutex_lock (&ctx->lock);
                pthread_mutex_lock (&pinode->mutex);
                {
			reqlock->pinode = pinode;

                        ret = __lock_entrylk (this, pinode, reqlock, nonblock, dom);
			if (ret == 0) {
				reqlock->frame = NULL;
				op_ret = 0;
			} else {
				op_errno = -ret;
			}

			if (ctx && (!ret || !nonblock))
				list_add (&reqlock->client_list,
					  &ctx->entrylk_lockers);

			if (ret == -EAGAIN && !nonblock) {
				/* blocked */
				unwind = 0;
			} else {
				__pl_entrylk_unref (reqlock);
			}

                        /* For all but the case where a non-blocking lock
                         * attempt fails, the extra ref taken before the switch
                         * block must be negated.
                         */
                        if ((ret == -EAGAIN) && (nonblock))
                                need_inode_unref = _gf_true;
                }
                pthread_mutex_unlock (&pinode->mutex);
		if (ctx)
			pthread_mutex_unlock (&ctx->lock);
		break;

        case ENTRYLK_UNLOCK:
		if (ctx)
			pthread_mutex_lock (&ctx->lock);
                pthread_mutex_lock (&pinode->mutex);
                {
                        /* Irrespective of whether unlock succeeds or not,
                         * the extra inode ref that was done before the switch
                         * block must be negated. Towards this,
                         * @need_inode_unref flag is set unconditionally here.
                         */
                        need_inode_unref = _gf_true;
                        unlocked = __unlock_entrylk (dom, reqlock);
			if (unlocked) {
				list_del_init (&unlocked->client_list);
				__pl_entrylk_unref (unlocked);
				op_ret = 0;
			} else {
				op_errno = EINVAL;
			}
			__pl_entrylk_unref (reqlock);
                }
                pthread_mutex_unlock (&pinode->mutex);
		if (ctx)
			pthread_mutex_unlock (&ctx->lock);

		grant_blocked_entry_locks (this, pinode, dom);

                break;

        default:
                inode_unref (pinode->inode);
                gf_log (this->name, GF_LOG_ERROR,
                        "Unexpected case in entrylk (cmd=%d). Please file"
                        "a bug report at http://bugs.gluster.com", cmd);
                goto out;
        }
        /* The following (extra) unref corresponds to the ref that
         * was done at the time the lock was granted.
         */
        if ((cmd == ENTRYLK_UNLOCK) && (op_ret == 0))
                inode_unref (pinode->inode);

out:

        if (need_inode_unref)
                inode_unref (pinode->inode);

        if (unwind) {
                entrylk_trace_out (this, frame, volume, fd, loc, basename,
                                   cmd, type, op_ret, op_errno);
unwind:
                STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno, NULL);
        } else {
                entrylk_trace_block (this, frame, volume, fd, loc, basename,
                                     cmd, type);
        }

        return 0;
}

/**
 * pl_entrylk:
 *
 * Locking on names (directory entries)
 */

int
pl_entrylk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, const char *basename,
            entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        pl_common_entrylk (frame, this, volume, loc->inode, basename, cmd,
                           type, loc, NULL, xdata);

        return 0;
}


/**
 * pl_fentrylk:
 *
 * Locking on names (directory entries)
 */

int
pl_fentrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, const char *basename,
             entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        pl_common_entrylk (frame, this, volume, fd->inode, basename, cmd,
                           type, NULL, fd, xdata);

        return 0;
}


static void
pl_entrylk_log_cleanup (pl_entry_lock_t *lock)
{
	pl_inode_t *pinode = NULL;

	pinode = lock->pinode;

        gf_log (THIS->name, GF_LOG_WARNING,
                "releasing lock on %s held by "
                "{client=%p, pid=%"PRId64" lk-owner=%s}",
                uuid_utoa (pinode->gfid), lock->client,
                (uint64_t) lock->client_pid, lkowner_utoa (&lock->owner));
}


/* Release all entrylks from this client */
int
pl_entrylk_client_cleanup (xlator_t *this, pl_ctx_t *ctx)
{
        pl_entry_lock_t *tmp = NULL;
        pl_entry_lock_t *l = NULL;
	pl_dom_list_t *dom = NULL;
        pl_inode_t *pinode = NULL;

        struct list_head released;
        struct list_head unwind;

        INIT_LIST_HEAD (&released);
        INIT_LIST_HEAD (&unwind);

	pthread_mutex_lock (&ctx->lock);
        {
                list_for_each_entry_safe (l, tmp, &ctx->entrylk_lockers,
					  client_list) {
			pl_entrylk_log_cleanup (l);

			pinode = l->pinode;

			pthread_mutex_lock (&pinode->mutex);
			{
                        /* If the entrylk object is part of granted list but not
                         * blocked list, then perform the following actions:
                         * i.   delete the object from granted list;
                         * ii.  grant other locks (from other clients) that may
                         *      have been blocked on this entrylk; and
                         * iii. unref the object.
                         *
                         * If the entrylk object (L1) is part of both granted
                         * and blocked lists, then this means that a parallel
                         * unlock on another entrylk (L2 say) may have 'granted'
                         * L1 and added it to 'granted' list in
                         * __grant_blocked_entry_locks() (although using the
                         * 'blocked_locks' member). In that case, the cleanup
                         * codepath must try and grant other overlapping
                         * blocked entrylks from other clients, now that L1 is
                         * out of their way and then unref L1 in the end, and
                         * leave it to the other thread (the one executing
                         * unlock codepath) to unwind L1's frame, delete it from
                         * blocked_locks list, and perform the last unref on L1.
                         *
                         * If the entrylk object (L1) is part of blocked list
                         * only, the cleanup code path must:
                         * i.   delete it from the blocked_locks list inside
                         *      this critical section,
                         * ii.  unwind its frame with EAGAIN,
                         * iii. try and grant blocked entry locks from other
                         *      clients that were otherwise grantable, but were
                         *      blocked to avoid leaving L1 to starve forever.
                         * iv.  unref the object.
                         */
                                list_del_init (&l->client_list);

                                if (!list_empty (&l->domain_list)) {
                                        list_del_init (&l->domain_list);
                                        list_add_tail (&l->client_list,
                                                       &released);
                                } else {
                                        list_del_init (&l->blocked_locks);
                                        list_add_tail (&l->client_list,
                                                       &unwind);
                                }
                        }
			pthread_mutex_unlock (&pinode->mutex);
                }
	}
        pthread_mutex_unlock (&ctx->lock);

        list_for_each_entry_safe (l, tmp, &unwind, client_list) {
                list_del_init (&l->client_list);

		if (l->frame)
			STACK_UNWIND_STRICT (entrylk, l->frame, -1, EAGAIN,
					     NULL);
                list_add_tail (&l->client_list, &released);
        }

        list_for_each_entry_safe (l, tmp, &released, client_list) {
                list_del_init (&l->client_list);

		pinode = l->pinode;

		dom = get_domain (pinode, l->volume);

		grant_blocked_entry_locks (this, pinode, dom);

		pthread_mutex_lock (&pinode->mutex);
		{
			__pl_entrylk_unref (l);
		}
		pthread_mutex_unlock (&pinode->mutex);
                inode_unref (pinode->inode);
        }

        return 0;
}


int32_t
__get_entrylk_count (xlator_t *this, pl_inode_t *pl_inode)
{
        int32_t            count = 0;
        pl_entry_lock_t   *lock  = NULL;
        pl_dom_list_t     *dom   = NULL;

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                list_for_each_entry (lock, &dom->entrylk_list, domain_list) {
                        count++;
                }

                list_for_each_entry (lock, &dom->blocked_entrylks, blocked_locks) {
                        count++;
                }

        }

        return count;
}

int32_t
get_entrylk_count (xlator_t *this, inode_t *inode)
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
                count = __get_entrylk_count (this, pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}
