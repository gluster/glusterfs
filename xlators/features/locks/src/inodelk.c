/*
  Copyright (c) 2006, 2007, 2008 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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
__delete_inode_lock (pl_inode_lock_t *lock)
{
        list_del (&lock->list);
}

void
__destroy_inode_lock (pl_inode_lock_t *lock)
{
        GF_FREE (lock);
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
static int same_inodelk_owner (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return ((l1->owner == l2->owner) &&
                (l1->transport  == l2->transport));
}

/* Returns true if the 2 inodelks conflict with each other */
static int
inodelk_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return (inodelk_overlap (l1, l2) &&
                inodelk_type_conflict (l1, l2));
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
                if (inodelk_conflict (lock, l)) {
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

        if (list_empty (&dom->blocked_entrylks))
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

        list_for_each_entry (lock, &dom->entrylk_list, list) {
                if (same_inodelk_owner (lock, newlock))
                        return 1;
        }

        list_for_each_entry (lock, &dom->blocked_entrylks, blocked_locks) {
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
        if (conf){
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) lk-owner:%"PRIu64" %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lock->owner,
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }

        if (__blocked_lock_conflict (dom, lock) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_TRACE,
                        "Lock is grantable, but blocking to prevent starvation");
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lock->owner,
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }
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
                gf_log (this->name, GF_LOG_DEBUG,
                        " Matching lock not found for unlock");
                goto out;
        }
        __delete_inode_lock (conf);
        gf_log (this->name, GF_LOG_DEBUG,
                " Matching lock found for unlock");
        __destroy_inode_lock (lock);


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
grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode, pl_dom_list_t *dom)
{
        struct list_head granted;
        pl_inode_lock_t *lock;
        pl_inode_lock_t *tmp;

        INIT_LIST_HEAD (&granted);

        if (list_empty (&dom->blocked_inodelks)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No blocked locks to be granted for domain: %s", dom->domain);
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_inode_locks (this, pl_inode, &granted, dom);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" => Granted",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lock->owner,
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);

                pl_trace_out (this, lock->frame, NULL, NULL, F_SETLKW,
                              &lock->user_flock, 0, 0, lock->volume);

                STACK_UNWIND_STRICT (inodelk, lock->frame, 0, 0);
        }

}

/* Release all inodelks from this transport */
static int
release_inode_locks_of_transport (xlator_t *this, pl_dom_list_t *dom,
                                  inode_t *inode, void *trans)
{
        pl_inode_lock_t *tmp = NULL;
        pl_inode_lock_t *l = NULL;

        pl_inode_t * pinode = NULL;

        struct list_head granted;
        struct list_head released;

        char *path = NULL;

        INIT_LIST_HEAD (&granted);
        INIT_LIST_HEAD (&released);

        pinode = pl_inode_get (this, inode);

        pthread_mutex_lock (&pinode->mutex);
        {

                list_for_each_entry_safe (l, tmp, &dom->blocked_inodelks, blocked_locks) {
                        if (l->transport != trans)
                                continue;

                        list_del_init (&l->blocked_locks);

                        if (inode_path (inode, NULL, &path) < 0) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "inode_path failed");
                                goto unlock;
                        }

                        gf_log (this->name, GF_LOG_TRACE,
                                "releasing lock on %s held by "
                                "{transport=%p, pid=%"PRId64" lk-owner=%"PRIu64"}",
                                path, trans,
                                (uint64_t) l->client_pid,
                                l->owner);

                        list_add (&l->blocked_locks, &released);

                }

                list_for_each_entry_safe (l, tmp, &dom->inodelk_list, list) {
                        if (l->transport != trans)
                                continue;

                        __delete_inode_lock (l);
                        __destroy_inode_lock (l);


                        if (inode_path (inode, NULL, &path) < 0) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "inode_path failed");
                                goto unlock;
                        }

                        gf_log (this->name, GF_LOG_TRACE,
                                "releasing lock on %s held by "
                                "{transport=%p, pid=%"PRId64" lk-owner=%"PRIu64"}",
                                path, trans,
                                (uint64_t) l->client_pid,
                                l->owner);


                }
        }
unlock:
        if (path)
                GF_FREE (path);

        pthread_mutex_unlock (&pinode->mutex);

        list_for_each_entry_safe (l, tmp, &released, blocked_locks) {
                list_del_init (&l->blocked_locks);

                STACK_UNWIND_STRICT (inodelk, l->frame, -1, EAGAIN);
                GF_FREE (l);
        }

        grant_blocked_inode_locks (this, pinode, dom);
        return 0;
}


static int
pl_inode_setlk (xlator_t *this, pl_inode_t *pl_inode, pl_inode_lock_t *lock,
                int can_block,  pl_dom_list_t *dom)
{
        int ret = -EINVAL;
        pl_inode_lock_t *retlock = NULL;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                if (lock->fl_type != F_UNLCK) {
                        ret = __lock_inodelk (this, pl_inode, lock, can_block, dom);
                        if (ret == 0)
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" => OK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lock->owner,
                                        lock->fl_start,
                                        lock->fl_end);

                        if (ret == -EAGAIN)
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" => NOK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lock->owner,
                                        lock->user_flock.l_start,
                                        lock->user_flock.l_len);

                        goto out;
                }


                retlock = __inode_unlock_lock (this, lock, dom);
                if (!retlock) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Bad Unlock issued on Inode lock");
                        ret = -EINVAL;
                        goto out;
                }
                __destroy_inode_lock (retlock);

                ret = 0;


        }
out:
        pthread_mutex_unlock (&pl_inode->mutex);
        grant_blocked_inode_locks (this, pl_inode, dom);
        return ret;
}

/* Create a new inode_lock_t */
pl_inode_lock_t *
new_inode_lock (struct gf_flock *flock, void *transport, pid_t client_pid,
                uint64_t owner, const char *volume)

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

        lock->transport  = transport;
        lock->client_pid = client_pid;
        lock->owner      = owner;
        lock->volume     = volume;

        INIT_LIST_HEAD (&lock->list);
        INIT_LIST_HEAD (&lock->blocked_locks);

        return lock;
}

/* Common inodelk code called from pl_inodelk and pl_finodelk */
int
pl_common_inodelk (call_frame_t *frame, xlator_t *this,
                   const char *volume, inode_t *inode, int32_t cmd,
                   struct gf_flock *flock, loc_t *loc, fd_t *fd)
{
        int32_t op_ret   = -1;
        int32_t op_errno = 0;
        int     ret      = -1;
        int     can_block = 0;
        void *                  transport  = NULL;
        pid_t                   client_pid = -1;
        uint64_t                owner      = -1;
        pl_inode_t *            pinode     = NULL;
        pl_inode_lock_t *       reqlock    = NULL;
        pl_dom_list_t *                dom           = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (inode, unwind);
        VALIDATE_OR_GOTO (flock, unwind);

        if ((flock->l_start < 0) || (flock->l_len < 0)) {
                op_errno = EINVAL;
                goto unwind;
        }

        pl_trace_in (this, frame, fd, loc, cmd, flock, volume);

        transport  = frame->root->trans;
        client_pid = frame->root->pid;
        owner      = frame->root->lk_owner;

        pinode = pl_inode_get (this, inode);
        if (!pinode) {
                op_errno = ENOMEM;
                goto unwind;
        }

        dom = get_domain (pinode, volume);

        if (owner == 0) {
                /*
                  special case: this means release all locks
                  from this transport
                */
                gf_log (this->name, GF_LOG_TRACE,
                        "Releasing all locks from transport %p", transport);

                release_inode_locks_of_transport (this, dom, inode, transport);
                goto unwind;
        }

        reqlock = new_inode_lock (flock, transport, client_pid, owner, volume);

        if (!reqlock) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        reqlock->frame = frame;
        reqlock->this  = this;

        switch (cmd) {
        case F_SETLKW:
                can_block = 1;
                reqlock->frame = frame;
                reqlock->this  = this;

                /* fall through */

        case F_SETLK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct gf_flock));
                ret = pl_inode_setlk (this, pinode, reqlock,
                                      can_block, dom);

                if (ret < 0) {
                        if (can_block) {
                                pl_trace_block (this, frame, fd, loc,
                                                cmd, flock, volume);
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_TRACE, "returning EAGAIN");
                        op_errno = -ret;
                        __destroy_inode_lock (reqlock);
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
        if ((inode != NULL) && (flock !=NULL)) {
                pl_update_refkeeper (this, inode);
                pl_trace_out (this, frame, fd, loc, cmd, flock, op_ret, op_errno, volume);
        }

        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno);
out:
        return 0;
}

int
pl_inodelk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock)
{

        pl_common_inodelk (frame, this, volume, loc->inode, cmd, flock, loc, NULL);

        return 0;
}

int
pl_finodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock)
{

        pl_common_inodelk (frame, this, volume, fd->inode, cmd, flock, NULL, fd);

        return 0;

}


static int32_t
__get_inodelk_count (xlator_t *this, pl_inode_t *pl_inode)
{
        int32_t            count  = 0;
        pl_inode_lock_t   *lock   = NULL;
        pl_dom_list_t     *dom    = NULL;

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                list_for_each_entry (lock, &dom->inodelk_list, list) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                " XATTR DEBUG"
                                " domain: %s %s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" "
                                "state = Active",
                                dom->domain,
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lock->owner,
                                lock->user_flock.l_start,
                                lock->user_flock.l_len);

                        count++;
                }

                list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                " XATTR DEBUG"
                                " domain: %s %s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" "
                                "state = Blocked",
                                dom->domain,
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lock->owner,
                                lock->user_flock.l_start,
                                lock->user_flock.l_len);

                        count++;
                }

        }

        return count;
}

int32_t
get_inodelk_count (xlator_t *this, inode_t *inode)
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
                count = __get_inodelk_count (this, pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}
