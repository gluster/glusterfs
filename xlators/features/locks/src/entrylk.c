/*
  Copyright (c) 2006, 2007, 2008 Gluster, Inc. <http://www.gluster.com>
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

static pl_entry_lock_t *
new_entrylk_lock (pl_inode_t *pinode, const char *basename, entrylk_type type,
		  transport_t *trans, pid_t client_pid, const char *volume)
{
	pl_entry_lock_t *newlock = NULL;

	newlock = CALLOC (1, sizeof (pl_entry_lock_t));
	if (!newlock) {
		goto out;
	}

	newlock->basename       = basename ? strdup (basename) : NULL;
	newlock->type           = type;
	newlock->trans          = trans;
	newlock->volume         = volume;
        newlock->client_pid     = client_pid;


	INIT_LIST_HEAD (&newlock->domain_list);
	INIT_LIST_HEAD (&newlock->blocked_locks);

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
        return ((l1->client_pid == l2->client_pid) &&
		(l1->trans  == l2->trans));
}


/**
 * lock_grantable - is this lock grantable?
 * @inode: inode in which to look
 * @basename: name we're trying to lock
 * @type: type of lock
 */
static pl_entry_lock_t *
__lock_grantable (pl_dom_list_t *dom, const char *basename, entrylk_type type)
{
	pl_entry_lock_t *lock = NULL;

	if (list_empty (&dom->entrylk_list))
		return NULL;

	list_for_each_entry (lock, &dom->entrylk_list, domain_list) {
		if (names_conflict (lock->basename, basename))
			return lock;
	}

	return NULL;
}

static pl_entry_lock_t *
__blocked_lock_conflict (pl_dom_list_t *dom, const char *basename, entrylk_type type)
{
	pl_entry_lock_t *lock = NULL;

	if (list_empty (&dom->blocked_entrylks))
		return NULL;

	list_for_each_entry (lock, &dom->blocked_entrylks, blocked_locks) {
		if (names_conflict (lock->basename, basename))
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

/**
 * __lock_name - lock a name in a directory
 * @inode: inode for the directory in which to lock
 * @basename: name of the entry to lock
 *            if null, lock the entire directory
 *
 * the entire directory being locked is represented as: a single
 * pl_entry_lock_t present in the entrylk_locks list with its
 * basename = NULL
 */

int
__lock_name (pl_inode_t *pinode, const char *basename, entrylk_type type,
	     call_frame_t *frame, pl_dom_list_t *dom, xlator_t *this, int nonblock)
{
	pl_entry_lock_t *lock    = NULL;
	pl_entry_lock_t *conf    = NULL;
	transport_t     *trans   = NULL;
        pid_t        client_pid = 0;

	int ret = -EINVAL;

	trans = frame->root->trans;
        client_pid = frame->root->pid;

	lock = new_entrylk_lock (pinode, basename, type, trans, client_pid, dom->domain);
	if (!lock) {
		ret = -ENOMEM;
		goto out;
	}

	conf = __lock_grantable (dom, basename, type);
	if (conf) {
		ret = -EAGAIN;
		if (nonblock)
			goto out;

		lock->frame   = frame;
		lock->this    = this;

		list_add_tail (&lock->blocked_locks, &dom->blocked_entrylks);

		gf_log (this->name, GF_LOG_TRACE,
			"Blocking lock: {pinode=%p, basename=%s}",
			pinode, basename);

		goto out;
	}

        if ( __blocked_lock_conflict (dom, basename, type) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (nonblock)
                        goto out;
                lock->frame     = frame;
                lock->this      = this;

                list_add_tail (&lock->blocked_locks, &dom->blocked_entrylks);

                gf_log (this->name, GF_LOG_TRACE,
                        "Lock is grantable, but blocking to prevent starvation");
		gf_log (this->name, GF_LOG_TRACE,
			"Blocking lock: {pinode=%p, basename=%s}",
			pinode, basename);

		goto out;
        }
        switch (type) {

	case ENTRYLK_WRLCK:
		list_add (&lock->domain_list, &dom->entrylk_list);
		break;

        default:

          gf_log (this->name, GF_LOG_DEBUG,
                  "Invalid type for entrylk specified: %d", type);
          ret = -EINVAL;
          goto out;
	}

	ret = 0;
out:
	return ret;
}

/**
 * __unlock_name - unlock a name in a directory
 * @inode: inode for the directory to unlock in
 * @basename: name of the entry to unlock
 *            if null, unlock the entire directory
 */

pl_entry_lock_t *
__unlock_name (pl_dom_list_t *dom, const char *basename, entrylk_type type)
{
	pl_entry_lock_t *lock = NULL;
	pl_entry_lock_t *ret_lock = NULL;

	lock = __find_most_matching_lock (dom, basename);

	if (!lock) {
		gf_log ("locks", GF_LOG_DEBUG,
			"unlock on %s (type=ENTRYLK_WRLCK) attempted but no matching lock found",
			basename);
		goto out;
	}

	if (names_equal (lock->basename, basename)
	    && lock->type == type) {

		if (type == ENTRYLK_WRLCK) {
			list_del (&lock->domain_list);
			ret_lock = lock;
		}
	} else {
		gf_log ("locks", GF_LOG_DEBUG,
			"Unlock for a non-existing lock!");
		goto out;
	}

out:
	return ret_lock;
}


void
__grant_blocked_entry_locks (xlator_t *this, pl_inode_t *pl_inode,
			     pl_dom_list_t *dom, struct list_head *granted)
{
	int              bl_ret = 0;
	pl_entry_lock_t *bl   = NULL;
	pl_entry_lock_t *tmp  = NULL;

	list_for_each_entry_safe (bl, tmp, &dom->blocked_entrylks,
				  blocked_locks) {

                if (__lock_grantable (dom, bl->basename, bl->type))
                        continue;

		list_del_init (&bl->blocked_locks);

		/* TODO: error checking */

		gf_log ("locks", GF_LOG_TRACE,
			"Trying to unblock: {pinode=%p, basename=%s}",
			pl_inode, bl->basename);

		bl_ret = __lock_name (pl_inode, bl->basename, bl->type,
				      bl->frame, dom, bl->this, 0);

		if (bl_ret == 0) {
			list_add (&bl->blocked_locks, granted);
		} else {
			if (bl->basename)
				FREE (bl->basename);
			FREE (bl);
		}
	}
	return;
}

/* Grants locks if possible which are blocked on a lock */
void
grant_blocked_entry_locks (xlator_t *this, pl_inode_t *pl_inode,
			   pl_entry_lock_t *unlocked, pl_dom_list_t *dom)
{
	struct list_head  granted_list;
	pl_entry_lock_t  *tmp = NULL;
	pl_entry_lock_t  *lock = NULL;

	INIT_LIST_HEAD (&granted_list);

	pthread_mutex_lock (&pl_inode->mutex);
	{
		__grant_blocked_entry_locks (this, pl_inode, dom, &granted_list);
	}
	pthread_mutex_unlock (&pl_inode->mutex);

	list_for_each_entry_safe (lock, tmp, &granted_list, blocked_locks) {
		list_del_init (&lock->blocked_locks);

		STACK_UNWIND_STRICT (entrylk, lock->frame, 0, 0);

		FREE (lock->basename);
		FREE (lock);
	}

	FREE (unlocked->basename);
	FREE (unlocked);

	return;
}

/**
 * release_entry_locks_for_transport: release all entry locks from this
 * transport for this loc_t
 */

static int
release_entry_locks_for_transport (xlator_t *this, pl_inode_t *pinode,
				   pl_dom_list_t *dom, transport_t *trans)
{
	pl_entry_lock_t  *lock = NULL;
	pl_entry_lock_t  *tmp = NULL;
	struct list_head  granted;

	INIT_LIST_HEAD (&granted);

	pthread_mutex_lock (&pinode->mutex);
	{
		if (list_empty (&dom->entrylk_list)) {
			goto unlock;
		}

		list_for_each_entry_safe (lock, tmp, &dom->entrylk_list,
					  domain_list) {
			if (lock->trans != trans)
				continue;

			list_del_init (&lock->domain_list);

			gf_log (this->name, GF_LOG_TRACE,
                                "releasing lock on  held by "
                                "{transport=%p}",trans);;

			FREE (lock->basename);
			FREE (lock);
		}

		__grant_blocked_entry_locks (this, pinode, dom, &granted);

	}
unlock:
	pthread_mutex_unlock (&pinode->mutex);

	list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
		list_del_init (&lock->blocked_locks);

		STACK_UNWIND_STRICT (entrylk, lock->frame, 0, 0);

		if (lock->basename)
			FREE (lock->basename);
		FREE (lock);
	}

	return 0;
}

/* Common entrylk code called by pl_entrylk and pl_fentrylk */
int
pl_common_entrylk (call_frame_t *frame, xlator_t *this,
	    const char *volume, inode_t *inode, const char *basename,
	    entrylk_cmd cmd, entrylk_type type)
{
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	transport_t * transport = NULL;
	pid_t pid = -1;

	pl_inode_t *       pinode = NULL;
	int                ret    = -1;
	pl_entry_lock_t   *unlocked = NULL;
	char               unwind = 1;

	pl_dom_list_t	  *dom = NULL;

	pinode = pl_inode_get (this, inode);
	if (!pinode) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto out;
	}

	dom = get_domain (pinode, volume);
	if (!dom){
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
		op_errno = ENOMEM;
		goto out;
	}

	pid       = frame->root->pid;
	transport = frame->root->trans;

	if (pid == 0) {
		/*
		   this is a special case that means release
		   all locks from this transport
		*/

		gf_log (this->name, GF_LOG_TRACE,
			"Releasing locks for transport %p", transport);

		release_entry_locks_for_transport (this, pinode, dom, transport);
		op_ret = 0;

		goto out;
	}

	switch (cmd) {
	case ENTRYLK_LOCK:
		pthread_mutex_lock (&pinode->mutex);
		{
			ret = __lock_name (pinode, basename, type,
					   frame, dom, this, 0);
		}
		pthread_mutex_unlock (&pinode->mutex);

		if (ret < 0) {
			if (ret == -EAGAIN)
				unwind = 0;
			op_errno = -ret;
			goto out;
		}

		break;

	case ENTRYLK_LOCK_NB:
		pthread_mutex_lock (&pinode->mutex);
		{
			ret = __lock_name (pinode, basename, type,
					   frame, dom, this, 1);
		}
		pthread_mutex_unlock (&pinode->mutex);

		if (ret < 0) {
			op_errno = -ret;
			goto out;
		}

		break;

	case ENTRYLK_UNLOCK:
		pthread_mutex_lock (&pinode->mutex);
		{
			 unlocked = __unlock_name (dom, basename, type);
		}
		pthread_mutex_unlock (&pinode->mutex);

		if (unlocked)
			grant_blocked_entry_locks (this, pinode, unlocked, dom);

		break;

	default:
		gf_log (this->name, GF_LOG_ERROR,
			"Unexpected case in entrylk (cmd=%d). Please file"
                        "a bug report at http://bugs.gluster.com", cmd);
		goto out;
	}

	op_ret = 0;
out:
        pl_update_refkeeper (this, inode);
	if (unwind) {
		STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno);
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
	    entrylk_cmd cmd, entrylk_type type)
{

  pl_common_entrylk (frame, this, volume, loc->inode, basename, cmd, type);

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
	     entrylk_cmd cmd, entrylk_type type)
{

  pl_common_entrylk (frame, this, volume, fd->inode, basename, cmd, type);

	return 0;
}
