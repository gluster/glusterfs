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
#include "common.h"

void
__delete_reserve_lock (posix_lock_t *lock)
{
        list_del (&lock->list);
}

void
__destroy_reserve_lock (posix_lock_t *lock)
{
        GF_FREE (lock);
}

/* Return true if the two reservelks have exactly same lock boundaries */
int
reservelks_equal (posix_lock_t *l1, posix_lock_t *l2)
{
        if ((l1->fl_start == l2->fl_start) &&
            (l1->fl_end == l2->fl_end))
                return 1;

        return 0;
}

/* Determine if lock is grantable or not */
static posix_lock_t *
__reservelk_grantable (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        xlator_t     *this     = NULL;
        posix_lock_t *l        = NULL;
        posix_lock_t *ret_lock = NULL;

        this = THIS;

        if (list_empty (&pl_inode->reservelk_list)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No reservelks in list");
                goto out;
        }
        list_for_each_entry (l, &pl_inode->reservelk_list, list){
                if (reservelks_equal (lock, l)) {
                        ret_lock = l;
                        break;
                }
        }
out:
        return ret_lock;
}

static int
__same_owner_reservelk (posix_lock_t *l1, posix_lock_t *l2)
{
        return (is_same_lkowner (&l1->owner, &l2->owner));

}

static posix_lock_t *
__matching_reservelk (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *l = NULL;

        if (list_empty (&pl_inode->reservelk_list)) {
            gf_log ("posix-locks", GF_LOG_TRACE,
                    "reservelk list empty");
            return NULL;
        }

        list_for_each_entry (l, &pl_inode->reservelk_list, list) {
                if (reservelks_equal (l, lock)) {
                        gf_log ("posix-locks", GF_LOG_TRACE,
                                "equal reservelk found");
                        break;
                }
        }

        return l;
}

static int
__reservelk_conflict (xlator_t *this, pl_inode_t *pl_inode,
                      posix_lock_t *lock)
{
        posix_lock_t *conf = NULL;
        int ret = 0;

        conf = __matching_reservelk (pl_inode, lock);
        if (conf) {
                gf_log (this->name, GF_LOG_TRACE,
                        "Matching reservelk found");
                if (__same_owner_reservelk (lock, conf)) {
                        list_del_init (&conf->list);
                        gf_log (this->name, GF_LOG_TRACE,
                                "Removing the matching reservelk for setlk to progress");
                        GF_FREE (conf);
                        ret = 0;
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "Conflicting reservelk found");
                        ret = 1;
                }

        }
        return ret;

}

int
pl_verify_reservelk (xlator_t *this, pl_inode_t *pl_inode,
                     posix_lock_t *lock, int can_block)
{
        int ret = 0;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                if (__reservelk_conflict (this, pl_inode, lock)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "Found conflicting reservelk. Blocking until reservelk is unlocked.");
                        lock->blocked = can_block;
                        list_add_tail (&lock->list, &pl_inode->blocked_calls);
                        ret = -1;
                        goto unlock;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "no conflicting reservelk found. Call continuing");
                ret = 0;

        }
unlock:
        pthread_mutex_unlock (&pl_inode->mutex);

        return ret;

}


/* Determines if lock can be granted and adds the lock. If the lock
 * is blocking, adds it to the blocked_reservelks.
 */
static int
__lock_reservelk (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
                  int can_block)
{
        posix_lock_t *conf = NULL;
        int ret = -EINVAL;

        conf = __reservelk_grantable (pl_inode, lock);
        if (conf){
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                list_add_tail (&lock->list, &pl_inode->blocked_reservelks);

                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }

        list_add (&lock->list, &pl_inode->reservelk_list);

        ret = 0;

out:
        return ret;
}

static posix_lock_t *
find_matching_reservelk (posix_lock_t *lock, pl_inode_t *pl_inode)
{
        posix_lock_t *l = NULL;
        list_for_each_entry (l, &pl_inode->reservelk_list, list) {
                if (reservelks_equal (l, lock))
                        return l;
        }
        return NULL;
}

/* Set F_UNLCK removes a lock which has the exact same lock boundaries
 * as the UNLCK lock specifies. If such a lock is not found, returns invalid
 */
static posix_lock_t *
__reserve_unlock_lock (xlator_t *this, posix_lock_t *lock, pl_inode_t *pl_inode)
{

        posix_lock_t *conf = NULL;

        conf = find_matching_reservelk (lock, pl_inode);
        if (!conf) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " Matching lock not found for unlock");
                goto out;
        }
        __delete_reserve_lock (conf);
        gf_log (this->name, GF_LOG_DEBUG,
                " Matching lock found for unlock");

out:
        return conf;


}

static void
__grant_blocked_reserve_locks (xlator_t *this, pl_inode_t *pl_inode,
                               struct list_head *granted)
{
        int              bl_ret = 0;
        posix_lock_t *bl = NULL;
        posix_lock_t *tmp = NULL;

        struct list_head blocked_list;

        INIT_LIST_HEAD (&blocked_list);
        list_splice_init (&pl_inode->blocked_reservelks, &blocked_list);

        list_for_each_entry_safe (bl, tmp, &blocked_list, list) {

                list_del_init (&bl->list);

                bl_ret = __lock_reservelk (this, pl_inode, bl, 1);

                if (bl_ret == 0) {
                        list_add (&bl->list, granted);
                }
        }
        return;
}

/* Grant all reservelks blocked on lock(s) */
void
grant_blocked_reserve_locks (xlator_t *this, pl_inode_t *pl_inode)
{
        struct list_head granted;
        posix_lock_t *lock = NULL;
        posix_lock_t *tmp = NULL;

        INIT_LIST_HEAD (&granted);

        if (list_empty (&pl_inode->blocked_reservelks)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No blocked locks to be granted");
                return;
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_reserve_locks (this, pl_inode, &granted);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted, list) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => Granted",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);

                STACK_UNWIND_STRICT (lk, lock->frame, 0, 0, &lock->user_flock,
                                     NULL);
        }

}

static void
__grant_blocked_lock_calls (xlator_t *this, pl_inode_t *pl_inode,
                            struct list_head *granted)
{
        int              bl_ret = 0;
        posix_lock_t *bl = NULL;
        posix_lock_t *tmp = NULL;

        struct list_head blocked_list;

        INIT_LIST_HEAD (&blocked_list);
        list_splice_init (&pl_inode->blocked_reservelks, &blocked_list);

        list_for_each_entry_safe (bl, tmp, &blocked_list, list) {

                list_del_init (&bl->list);

                bl_ret = pl_verify_reservelk (this, pl_inode, bl, bl->blocked);

                if (bl_ret == 0) {
                        list_add_tail (&bl->list, granted);
                }
        }
        return;
}

void
grant_blocked_lock_calls (xlator_t *this, pl_inode_t *pl_inode)
{
        struct list_head granted;
        posix_lock_t *lock = NULL;
        posix_lock_t *tmp = NULL;
        fd_t         *fd  = NULL;

        int can_block = 0;
        int32_t cmd = 0;
        int ret = 0;

        if (list_empty (&pl_inode->blocked_calls)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No blocked lock calls to be granted");
                return;
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_lock_calls (this, pl_inode, &granted);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted, list) {
                fd = fd_from_fdnum (lock);

                if (lock->blocked) {
                        can_block = 1;
                        cmd = F_SETLKW;
                }
                else
                        cmd = F_SETLK;

                lock->blocked = 0;
                ret = pl_setlk (this, pl_inode, lock, can_block);
                if (ret == -1) {
                        if (can_block) {
                                pl_trace_block (this, lock->frame, fd, NULL,
                                                cmd, &lock->user_flock, NULL);
                                continue;
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG, "returning EAGAIN");
                                pl_trace_out (this, lock->frame, fd, NULL, cmd,
                                              &lock->user_flock, -1, EAGAIN, NULL);
                                pl_update_refkeeper (this, fd->inode);
                                STACK_UNWIND_STRICT (lk, lock->frame, -1,
                                                     EAGAIN, &lock->user_flock,
                                                     NULL);
                                __destroy_lock (lock);
                        }
                }

        }

}


int
pl_reserve_unlock (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *retlock = NULL;
        int ret = -1;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                retlock = __reserve_unlock_lock (this, lock, pl_inode);
                if (!retlock) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Bad Unlock issued on Inode lock");
                        ret = -EINVAL;
                        goto out;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "Reservelk Unlock successful");
                __destroy_reserve_lock (retlock);
                ret = 0;
        }
out:
        pthread_mutex_unlock (&pl_inode->mutex);

        grant_blocked_reserve_locks (this, pl_inode);
        grant_blocked_lock_calls (this, pl_inode);

        return ret;

}

int
pl_reserve_setlk (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
                  int can_block)
{
        int ret = -EINVAL;

        pthread_mutex_lock (&pl_inode->mutex);
        {

                        ret = __lock_reservelk (this, pl_inode, lock, can_block);
                        if (ret < 0)
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => NOK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->user_flock.l_start,
                                        lock->user_flock.l_len);
                        else
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => OK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->fl_start,
                                        lock->fl_end);

        }
        pthread_mutex_unlock (&pl_inode->mutex);
        return ret;
}
