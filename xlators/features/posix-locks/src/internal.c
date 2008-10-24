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


static int
set_new_pinode (dict_t *ctx, xlator_t *this, void **pinode_ret)
{
	pl_inode_t * pinode = NULL;
	int ret = -1;
	int op_ret = -1;
	int op_errno = 0;

	pinode = calloc (sizeof (pl_inode_t), 1);
	if (!pinode) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		op_errno = ENOMEM;
		goto out;
	}

	*pinode_ret = pinode;

	pthread_mutex_init (&pinode->dir_lock_mutex, NULL);
	INIT_LIST_HEAD (&pinode->gf_dir_locks);

	ret = dict_set_ptr (ctx, this->name, pinode);
	if (ret < 0) {
		op_errno = -ret;
		gf_log (this->name, GF_LOG_ERROR,
			"dict_set failed: %s", strerror (op_errno));
		goto out;
	}

	op_ret = 0;
out:
	return op_ret;
}


/**
 * pl_gf_file_lk: 
 *
 * This fop provides fcntl-style locking on files for internal
 * purposes. Locks held through this fop reside in a domain different
 * from those held by applications. This fop is for the use of AFR.
 */

int32_t 
pl_gf_file_lk (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, fd_t *fd, int32_t cmd,
	       struct flock *flock)
{
	int32_t op_ret   = -1;
	int32_t op_errno = 0;
	int     ret      = -1;

	posix_locks_private_t * priv       = NULL;
	transport_t *           transport  = NULL;
	pid_t                   client_pid = -1;
	pl_inode_t *            pinode     = NULL;
	posix_lock_t *          reqlock    = NULL;

	dict_t *                ctx        = NULL;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (loc, out);
	VALIDATE_OR_GOTO (flock, out);
	
	if ((flock->l_start < 0) || (flock->l_len < 0)) {
		op_errno = EINVAL;
		goto out;
	}

	transport  = frame->root->trans;
	client_pid = frame->root->pid;

	priv = (posix_locks_private_t *) this->private;

	VALIDATE_OR_GOTO (priv, out);

	if (loc && loc->inode && loc->inode->ctx) {
		ctx = loc->inode->ctx;
	}
	else if (fd && fd->inode && fd->inode->ctx) {
		ctx = fd->inode->ctx;
	}
	else {
		gf_log (this->name, GF_LOG_CRITICAL,
			"neither loc->inode->ctx nor fd->inode->ctx found!");
		op_errno = EINVAL;
		goto out;
	}

	pthread_mutex_lock (&priv->mutex);
	{
		ret = dict_get_ptr (ctx, this->name, (void **) &pinode);
		if (ret < 0) {
			ret = set_new_pinode (ctx, this, (void **) &pinode);
			if (ret < 0) {
				op_errno = -ret;
				goto unlock;
			}
		}

		reqlock = new_posix_lock (flock, transport, client_pid);
		switch (cmd) {
		case F_SETLK:
			ret = pl_setlk (pinode, reqlock, 0, GF_LOCK_INTERNAL);

			if (ret == -1) {
				op_errno = EAGAIN;
				goto unlock;
			}
			break;
		case F_SETLKW:
			reqlock->frame = frame;
			reqlock->this  = this;
			reqlock->fd    = fd;

                        /* TODO: free this */
			reqlock->user_flock = calloc (1, sizeof (struct flock));
			if (!reqlock->user_flock) {
				op_errno = ENOMEM;
				goto unlock;
			}

			memcpy (reqlock->user_flock, flock, sizeof (struct flock));

			ret = pl_setlk (pinode, reqlock, 1, GF_LOCK_INTERNAL);
			if (ret == -1)
				return -1; /* lock has been blocked */

			break;
		default:
			op_errno = ENOTSUP;
			gf_log (this->name, GF_LOG_ERROR,
				"lock command F_GETLK not supported for GF_FILE_LK (cmd=%d)", 
				cmd);
			goto unlock;
		}
	}

	op_ret = 0;
unlock:
	pthread_mutex_unlock (&priv->mutex);
out:
	if (op_ret == -1) {

	}
	
	STACK_UNWIND (frame, op_ret, op_errno);
	return op_ret;
}


/**
 * types_conflict - do two types of lock conflict?
 * @t1: type
 * @t2: type
 *
 * two read locks do not conflict
 * any other case conflicts
 */

static int
types_conflict (gf_dir_lk_type t1, gf_dir_lk_type t2)
{
	return !((t1 == GF_DIR_LK_RDLCK) && (t2 == GF_DIR_LK_RDLCK));
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
names_equal (const char *n1, const char *n2)
{
	return (n1 == NULL && n2 == NULL) || !strcmp (n1, n2);
}

/**
 * lock_grantable - is this lock grantable?
 * @inode: inode in which to look
 * @basename: name we're trying to lock
 * @type: type of lock
 */

static int
lock_grantable (pl_inode_t *pinode, const char *basename, gf_dir_lk_type type)
{
	pl_dir_lock_t *lock;
	if (list_empty (&pinode->gf_dir_locks))
		return 1;

	list_for_each_entry (lock, &pinode->gf_dir_locks, inode_list) {
		if (names_conflict (lock->basename, basename) &&
		    types_conflict (lock->type, type))
			return 0;
	}

	return 1;
}

/**
 * find_most_matching_lock - find the lock struct which most matches in order of:
 *                           lock on the exact basename ||
 *                           an all_names lock
 *                      
 *
 * @inode: inode in which to look
 * @basename: name to search for
 */

static pl_dir_lock_t * 
find_most_matching_lock (pl_inode_t *pinode, const char *basename)
{
	pl_dir_lock_t *lock;
	pl_dir_lock_t *all = NULL;
	pl_dir_lock_t *exact = NULL;

	if (list_empty (&pinode->gf_dir_locks)) 
		return NULL;

	list_for_each_entry (lock, &pinode->gf_dir_locks, inode_list) {
		if (all_names (lock->basename))
			all = lock;
		else if (names_equal (lock->basename, basename))
			exact = lock;
	}

	return (exact ? exact : all);
}


/**
 * insert_new_lock - insert a new dir lock into the inode with the given parameters
 * @pinode: inode to insert into
 * @basename: basename for the lock
 * @type: type of the lock
 */

static int
insert_new_lock (pl_inode_t *pinode, const char *basename, gf_dir_lk_type type)
{
	pl_dir_lock_t *newlock = NULL;
	int ret = -EINVAL;

	newlock = calloc (sizeof (pl_dir_lock_t), 1);
	if (!newlock) {
		ret = -ENOMEM;
		goto out;
	}

	newlock->basename = basename ? strdup (basename) : NULL;
	newlock->type     = type;
	if (type == GF_DIR_LK_RDLCK)
		newlock->read_count = 1;

	INIT_LIST_HEAD (&newlock->inode_list);
	list_add (&newlock->inode_list, &pinode->gf_dir_locks);

	ret = 0;
out:
	return ret;
}

/**
 * lock_name - lock a name in a directory
 * @inode: inode for the directory in which to lock
 * @basename: name of the entry to lock
 *            if null, lock the entire directory
 *            
 * the entire directory being locked is represented as: a single
 * pl_dir_lock_t present in the gf_dir_locks list with its
 * basename = NULL
 */

int
lock_name (pl_inode_t *pinode, const char *basename, gf_dir_lk_type type)
{
	pl_dir_lock_t *lock    = NULL;

	int ret = -EINVAL;
	
	pthread_mutex_lock (&pinode->dir_lock_mutex);
	{
		if (!lock_grantable (pinode, basename, type)) {
			ret = -EAGAIN;
			goto out;
		}
		
		switch (type) {
		case GF_DIR_LK_RDLCK:
			lock = find_most_matching_lock (pinode, basename);
			if (lock && names_equal (lock->basename, basename)) {
				lock->read_count++;
			} else {
				ret = insert_new_lock (pinode, basename, type);
				if (ret < 0)
					goto out;
			}
			break;

		case GF_DIR_LK_WRLCK:
			ret = insert_new_lock (pinode, basename, type);
			if (ret < 0)
				goto out;
			break;
		}

		ret = 0;
	}
out:
	pthread_mutex_unlock (&pinode->dir_lock_mutex);
	return ret;
}


/**
 * unlock_name - unlock a name in a directory
 * @inode: inode for the directory to unlock in
 * @basename: name of the entry to unlock
 *            if null, unlock the entire directory
 */

int
unlock_name (pl_inode_t *pinode, const char *basename, gf_dir_lk_type type)
{
	pl_dir_lock_t *lock = NULL;
	int ret             = -EINVAL;

	pthread_mutex_lock (&pinode->dir_lock_mutex);
	{
		lock = find_most_matching_lock (pinode, basename);
		
		if (!lock) {
			gf_log ("locks", GF_LOG_DEBUG,
				"unlock on %s (type=%s) attempted but no matching lock found",
				basename, type == GF_DIR_LK_RDLCK ? "GF_DIR_LK_RDLCK" : 
				"GF_DIR_LK_WRLCK");
			goto out;
		}

		if (names_equal (lock->basename, basename) &&
		    lock->type == type) {
			if (type == GF_DIR_LK_RDLCK) {
				lock->read_count--;
			}
			if (type == GF_DIR_LK_WRLCK || lock->read_count == 0) {
				list_del (&lock->inode_list);
			
				if (lock->basename)
					FREE (lock->basename);
				FREE (lock);
			}
		}
		else {
			gf_log ("locks", GF_LOG_ERROR,
				"unlock for a non-existing lock!");
			goto out;
		}
	
		ret = 0;
	}

out:
	pthread_mutex_unlock (&pinode->dir_lock_mutex);
	return ret;
}


/**
 * pl_gf_dir_lk:
 * 
 * Locking on names (directory entries)
 */

int32_t 
pl_gf_dir_lk (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *basename, 
	      gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
	int32_t op_ret   = -1;
	int32_t op_errno = 0;
	
	pl_inode_t *       pinode = NULL; 
	int                ret    = -1;

	LOCK (&loc->inode->lock);
	ret = dict_get_ptr (loc->inode->ctx, this->name, (void **) &pinode);
	if (ret < 0) {
		if (cmd == GF_DIR_LK_UNLOCK) {
			gf_log (this->name, GF_LOG_DEBUG,
				"cmd is GF_DIR_LK_UNLOCK but inode->ctx is not set!");

			UNLOCK (&loc->inode->lock);
			op_errno = -EINVAL;
			goto out;
		}

		ret = set_new_pinode (loc->inode->ctx, this, (void **) &pinode);
		
		UNLOCK (&loc->inode->lock);

		if (ret < 0) {
			op_errno = -ret;
			goto out;
		}
	} else {
		UNLOCK (&loc->inode->lock);
	}

	switch (cmd) {
	case GF_DIR_LK_LOCK:
		ret = lock_name (pinode, basename, type);
		if (ret < 0) {
			op_errno = -ret;
			goto out;
		}
		break;
	case GF_DIR_LK_UNLOCK:
		ret = unlock_name (pinode, basename, type);
		if (ret < 0) {
			op_errno = -ret;
			goto out;
		}
		break;
	default:
		gf_log (this->name, GF_LOG_ERROR,
			"unexpected case!");
		goto out;
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

