/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
	FREE (lock);
}

/* Check if 2 inodelks are conflicting on type. Only 2 shared locks don't conflict */
static int
inodelk_type_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
	if (l2->fl_type == F_WRLCK || l1->fl_type == F_WRLCK)
		return 1;

	return 0;
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
	return ((l1->client_pid == l2->client_pid) &&
                (l1->transport  == l2->transport));
}

/* Returns true if the 2 inodelks conflict with each other */
static int
inodelk_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
	if (same_inodelk_owner (l1, l2))
		return 0;

	if (!inodelk_overlap (l1, l2))
		return 0;

	return (inodelk_type_conflict(l1, l2));
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
__lock_inodelk (pl_inode_t *pl_inode, pl_inode_lock_t *lock,
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

			gf_log ("posix-locks", GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => Blocked",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len);


		goto out;
	}

        if (__blocked_lock_conflict (dom, lock) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log ("posix-locks", GF_LOG_TRACE,
                        "Lock is grantable, but blocking to prevent starvation");
		gf_log ("posix-locks", GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => Blocked",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
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
		if (inodelks_equal (l, lock))
			return l;
	}
	return NULL;
}

/* Set F_UNLCK removes a lock which has the exact same lock boundaries
 * as the UNLCK lock specifies. If such a lock is not found, returns invalid
 */
static pl_inode_lock_t *
__inode_unlock_lock (pl_inode_lock_t *lock, pl_dom_list_t *dom)
{

	pl_inode_lock_t *conf = NULL;

	conf = find_matching_inodelk (lock, dom);
	if (!conf) {
          gf_log ("posix-locks", GF_LOG_DEBUG,
                  " Matching lock not found for unlock");
		goto out;
        }
	__delete_inode_lock (conf);
          gf_log ("posix-locks", GF_LOG_DEBUG,
                  " Matching lock found for unlock");
        __destroy_inode_lock (lock);


out:
	return conf;


}
static void
__grant_blocked_inode_locks (pl_inode_t *pl_inode, pl_dom_list_t *dom)
{
	int	      bl_ret = 0;
	pl_inode_lock_t *bl = NULL;
	pl_inode_lock_t *tmp = NULL;

	list_for_each_entry_safe (bl, tmp, &dom->blocked_inodelks, blocked_locks) {

                if (__inodelk_grantable (dom, bl))
                        continue;

		list_del_init (&bl->blocked_locks);

		bl_ret = __lock_inodelk (pl_inode, bl, 1, dom);

		if (bl_ret == 0) {
                        gf_log ("posix-locks", GF_LOG_TRACE,
                                "%s (pid=%d) %"PRId64" - %"PRId64" => Granted",
                                bl->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                bl->client_pid,
                                bl->user_flock.l_start,
                                bl->user_flock.l_len);

                        STACK_UNWIND (bl->frame, 0, 0, &bl->user_flock);
                }
        }
	return;
}

/* Grant all inodelks blocked on a lock */
void
grant_blocked_inode_locks (pl_inode_t *pl_inode, pl_inode_lock_t *lock, pl_dom_list_t *dom)
{

        if (list_empty (&dom->blocked_inodelks)) {
                return;
        }


	__grant_blocked_inode_locks (pl_inode, dom);
        __destroy_inode_lock (lock);

}

/* Release all inodelks from this transport */
static int
release_inode_locks_of_transport (xlator_t *this, pl_dom_list_t *dom,
                                  inode_t *inode, transport_t *trans)
{
	pl_inode_lock_t *tmp = NULL;
	pl_inode_lock_t *l = NULL;

        pl_inode_t * pinode = NULL;

        struct list_head granted;

        char *path = NULL;

        INIT_LIST_HEAD (&granted);

        pinode = pl_inode_get (this, inode);

        pthread_mutex_lock (&pinode->mutex);
        {
                if (list_empty (&dom->inodelk_list)) {
                        goto unlock;
                }

                list_for_each_entry_safe (l, tmp, &dom->inodelk_list, list) {
                        if (l->transport != trans)
                                continue;

                        list_del_init (&l->list);

			grant_blocked_inode_locks (pinode, l, dom);

                        __delete_inode_lock (l);

                        inode_path (inode, NULL, &path);

                        gf_log (this->name, GF_LOG_TRACE,
                                "releasing lock on %s held by "
                                "{transport=%p, pid=%"PRId64"}",
                                path, trans,
                                (uint64_t) l->client_pid);

                        if (path)
                                FREE (path);

                }
        }
unlock:
        pthread_mutex_unlock (&pinode->mutex);

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
			ret = __lock_inodelk (pl_inode, lock, can_block, dom);
			if (ret == 0)
				gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => OK",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->fl_start,
				lock->fl_end);

			if (ret == -EAGAIN)
				gf_log (this->name, GF_LOG_TRACE,
					"%s (pid=%d) %"PRId64" - %"PRId64" => NOK",
					lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
					lock->client_pid,
					lock->user_flock.l_start,
					lock->user_flock.l_len);

			goto out;
		}


		retlock = __inode_unlock_lock (lock, dom);
		if (!retlock) {
			gf_log (this->name, GF_LOG_DEBUG,
				"Bad Unlock issued on Inode lock");
                        ret = -EINVAL;
                        goto out;
                }

			gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => OK",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len);
                        ret = 0;

		grant_blocked_inode_locks (pl_inode, retlock, dom);
	}
out:
	pthread_mutex_unlock (&pl_inode->mutex);
		return ret;
}

/* Create a new inode_lock_t */
pl_inode_lock_t *
new_inode_lock (struct flock *flock, transport_t *transport, pid_t client_pid, const char *volume)
{
	pl_inode_lock_t *lock = NULL;

	lock = CALLOC (1, sizeof (*lock));
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
	lock->volume     = volume;

	INIT_LIST_HEAD (&lock->list);
	INIT_LIST_HEAD (&lock->blocked_locks);

	return lock;
}

/* Common inodelk code called form pl_inodelk and pl_finodelk */
int
pl_common_inodelk (call_frame_t *frame, xlator_t *this,
	    const char *volume, inode_t *inode, int32_t cmd, struct flock *flock)
{
	int32_t op_ret   = -1;
	int32_t op_errno = 0;
	int     ret      = -1;
	int     can_block = 0;
	transport_t *           transport  = NULL;
	pid_t                   client_pid = -1;
	pl_inode_t *            pinode     = NULL;
	pl_inode_lock_t *          reqlock    = NULL;
	pl_dom_list_t *		dom	   = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (inode, out);
	VALIDATE_OR_GOTO (flock, out);

	if ((flock->l_start < 0) || (flock->l_len < 0)) {
		op_errno = EINVAL;
		goto unwind;
	}

	transport  = frame->root->trans;
	client_pid = frame->root->pid;

	pinode = pl_inode_get (this, inode);
	if (!pinode) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto unwind;
	}

	dom = get_domain (pinode, volume);

	if (client_pid == 0) {
		/*
		   special case: this means release all locks
		   from this transport
		*/
		gf_log (this->name, GF_LOG_TRACE,
			"Releasing all locks from transport %p", transport);

		release_inode_locks_of_transport (this, dom, inode, transport);
		goto unwind;
	}

	reqlock = new_inode_lock (flock, transport, client_pid, volume);
	if (!reqlock) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_ret = -1;
		op_errno = ENOMEM;
		goto unwind;
	}

	switch (cmd) {
	case F_SETLKW:
		can_block = 1;
		reqlock->frame = frame;
		reqlock->this  = this;

		/* fall through */

	case F_SETLK:
		memcpy (&reqlock->user_flock, flock, sizeof (struct flock));
		ret = pl_inode_setlk (this, pinode, reqlock,
				can_block, dom);

		if (ret < 0) {
                        if (can_block)
				goto out;

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
	STACK_UNWIND (frame, op_ret, op_errno);
out:
	return 0;
}

int
pl_inodelk (call_frame_t *frame, xlator_t *this,
	    const char *volume, loc_t *loc, int32_t cmd, struct flock *flock)
{

	pl_common_inodelk (frame, this, volume, loc->inode, cmd, flock);

	return 0;
}

int
pl_finodelk (call_frame_t *frame, xlator_t *this,
	     const char *volume, fd_t *fd, int32_t cmd, struct flock *flock)
{

	pl_common_inodelk (frame, this, volume, fd->inode, cmd, flock);

	return 0;

}
