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

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

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

#include "locks.h"


static int
__is_lock_grantable (pl_inode_t *pl_inode, posix_lock_t *lock,
		     gf_lk_domain_t dom);
static void
__insert_and_merge (pl_inode_t *pl_inode, posix_lock_t *lock,
		    gf_lk_domain_t dom);

#define DOMAIN_HEAD(pl_inode, dom) (dom == GF_LOCK_POSIX	\
				    ? &pl_inode->ext_list	\
				    : &pl_inode->int_list)

pl_inode_t *
pl_inode_get (xlator_t *this, inode_t *inode)
{
        uint64_t    tmp_pl_inode = 0;
	pl_inode_t *pl_inode = NULL;
	mode_t      st_mode = 0;
	int         ret = 0;

	ret = inode_ctx_get (inode, this,&tmp_pl_inode);
	if (ret == 0) {
                pl_inode = (pl_inode_t *)(long)tmp_pl_inode;
		goto out;
        }
	pl_inode = CALLOC (1, sizeof (*pl_inode));
	if (!pl_inode) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	st_mode  = inode->st_mode;
	if ((st_mode & S_ISGID) && !(st_mode & S_IXGRP))
		pl_inode->mandatory = 1;


	pthread_mutex_init (&pl_inode->mutex, NULL);

	INIT_LIST_HEAD (&pl_inode->dir_list);
	INIT_LIST_HEAD (&pl_inode->ext_list);
	INIT_LIST_HEAD (&pl_inode->int_list);
	INIT_LIST_HEAD (&pl_inode->rw_list);

	ret = inode_ctx_put (inode, this, (uint64_t)(long)(pl_inode));

out:
	return pl_inode;
}


/* Create a new posix_lock_t */
posix_lock_t *
new_posix_lock (struct flock *flock, transport_t *transport, pid_t client_pid)
{
	posix_lock_t *lock = NULL;

	lock = CALLOC (1, sizeof (posix_lock_t));
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

	INIT_LIST_HEAD (&lock->list);

	return lock;
}


/* Delete a lock from the inode's lock list */
void
__delete_lock (pl_inode_t *pl_inode, posix_lock_t *lock)
{
	list_del_init (&lock->list);
}


/* Destroy a posix_lock */
void
__destroy_lock (posix_lock_t *lock)
{
	free (lock);
}


/* Convert a posix_lock to a struct flock */
void
posix_lock_to_flock (posix_lock_t *lock, struct flock *flock)
{
	flock->l_pid   = lock->client_pid;
	flock->l_type  = lock->fl_type;
	flock->l_start = lock->fl_start;

	if (lock->fl_end == LLONG_MAX)
		flock->l_len = 0;
	else
		flock->l_len = lock->fl_end - lock->fl_start + 1;
}


/* Insert the lock into the inode's lock list */
static void
__insert_lock (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t dom)
{
	list_add_tail (&lock->list, DOMAIN_HEAD (pl_inode, dom));

	return;
}


/* Return true if the locks overlap, false otherwise */
int
locks_overlap (posix_lock_t *l1, posix_lock_t *l2)
{
	/* 
	   Note:
	   FUSE always gives us absolute offsets, so no need to worry 
	   about SEEK_CUR or SEEK_END
	*/

	return ((l1->fl_end >= l2->fl_start) &&
		(l2->fl_end >= l1->fl_start));
}


/* Return true if the locks have the same owner */
int
same_owner (posix_lock_t *l1, posix_lock_t *l2)
{
	return ((l1->client_pid == l2->client_pid) &&
		(l1->transport  == l2->transport));
}


/* Delete all F_UNLCK locks */
void
__delete_unlck_locks (pl_inode_t *pl_inode, gf_lk_domain_t dom)
{
	posix_lock_t *l = NULL;
	posix_lock_t *tmp = NULL;

	list_for_each_entry_safe (l, tmp, DOMAIN_HEAD (pl_inode, dom), list) {
		if (l->fl_type == F_UNLCK) {
			__delete_lock (pl_inode, l);
			__destroy_lock (l);
		}
	}
}


/* Add two locks */
static posix_lock_t *
add_locks (posix_lock_t *l1, posix_lock_t *l2)
{
	posix_lock_t *sum = NULL;

	sum = CALLOC (1, sizeof (posix_lock_t));
	if (!sum)
		return NULL;

	sum->fl_start = min (l1->fl_start, l2->fl_start);
	sum->fl_end   = max (l1->fl_end, l2->fl_end);

	return sum;
}

/* Subtract two locks */
struct _values {
	posix_lock_t *locks[3];
};

/* {big} must always be contained inside {small} */
static struct _values
subtract_locks (posix_lock_t *big, posix_lock_t *small)
{
	struct _values v = { .locks = {0, 0, 0} };
  
	if ((big->fl_start == small->fl_start) && 
	    (big->fl_end   == small->fl_end)) {  
		/* both edges coincide with big */
		v.locks[0] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_type = small->fl_type;
	}
	else if ((small->fl_start > big->fl_start) &&
		 (small->fl_end   < big->fl_end)) {
		/* both edges lie inside big */
		v.locks[0] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);
		v.locks[2] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[2]);

		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_end = small->fl_start - 1;

		memcpy (v.locks[1], small, sizeof (posix_lock_t));
		memcpy (v.locks[2], big, sizeof (posix_lock_t));
		v.locks[2]->fl_start = small->fl_end + 1;
	}
	/* one edge coincides with big */
	else if (small->fl_start == big->fl_start) {
		v.locks[0] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);
    
		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_start = small->fl_end + 1;
    
		memcpy (v.locks[1], small, sizeof (posix_lock_t));
	}
	else if (small->fl_end   == big->fl_end) {
		v.locks[0] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = CALLOC (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);

		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_end = small->fl_start - 1;
    
		memcpy (v.locks[1], small, sizeof (posix_lock_t));
	}
	else {
		gf_log ("posix-locks", GF_LOG_ERROR,
			"Unexpected case in subtract_locks. Please send "
                        "a bug report to gluster-devel@nongnu.org");
	}

	return v;
}

/* 
   Start searching from {begin}, and return the first lock that
   conflicts, NULL if no conflict
   If {begin} is NULL, then start from the beginning of the list
*/
static posix_lock_t *
first_overlap (pl_inode_t *pl_inode, posix_lock_t *lock,
	       gf_lk_domain_t dom)
{
	posix_lock_t *l = NULL;

	list_for_each_entry (l, DOMAIN_HEAD (pl_inode, dom), list) {
		if (l->blocked)
			continue;

		if (locks_overlap (l, lock))
			return l;
	}

	return NULL;
}



/* Return true if lock is grantable */
static int
__is_lock_grantable (pl_inode_t *pl_inode, posix_lock_t *lock,
		     gf_lk_domain_t dom)
{
	posix_lock_t *l = NULL;
	int           ret = 1;

	list_for_each_entry (l, DOMAIN_HEAD (pl_inode, dom), list) {
		if (!l->blocked && locks_overlap (lock, l)) {
			if (((l->fl_type == F_WRLCK)
			     || (lock->fl_type == F_WRLCK))
			    && (lock->fl_type != F_UNLCK)
			    && !same_owner (l, lock)) {
				ret = 0;
				break;
			}
		}
	}
	return ret;
}


extern void do_blocked_rw (pl_inode_t *);


static void
__insert_and_merge (pl_inode_t *pl_inode, posix_lock_t *lock,
		    gf_lk_domain_t dom)
{
	posix_lock_t  *conf = NULL;
	posix_lock_t  *t = NULL;
	posix_lock_t  *sum = NULL;
	int            i = 0;
	struct _values v = { .locks = {0, 0, 0} };

	list_for_each_entry_safe (conf, t, DOMAIN_HEAD (pl_inode, dom), list) {
		if (!locks_overlap (conf, lock))
			continue;

		if (same_owner (conf, lock)) {
			if (conf->fl_type == lock->fl_type) {
				sum = add_locks (lock, conf);

				sum->fl_type    = lock->fl_type;
				sum->transport  = lock->transport;
				sum->client_pid = lock->client_pid;

				__delete_lock (pl_inode, conf); 
				__destroy_lock (conf);

				__destroy_lock (lock);
				__insert_and_merge (pl_inode, sum, dom);

				return;
			} else {
				sum = add_locks (lock, conf);

				sum->fl_type    = conf->fl_type;
				sum->transport  = conf->transport;
				sum->client_pid = conf->client_pid;

				v = subtract_locks (sum, lock);
	
				__delete_lock (pl_inode, conf);
				__destroy_lock (conf);

				__delete_lock (pl_inode, lock);
				__destroy_lock (lock);

				__destroy_lock (sum);

				for (i = 0; i < 3; i++) {
					if (!v.locks[i])
						continue;

					if (v.locks[i]->fl_type == F_UNLCK) {
						__destroy_lock (v.locks[i]);
						continue;
					}
					__insert_and_merge (pl_inode,
							    v.locks[i], dom);
				}

				__delete_unlck_locks (pl_inode, dom);
				return; 
			}
		}

		if (lock->fl_type == F_UNLCK) {
			continue;
		}

		if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
			__insert_lock (pl_inode, lock, dom);
			return;
		}
	}

	/* no conflicts, so just insert */
	if (lock->fl_type != F_UNLCK) {
		__insert_lock (pl_inode, lock, dom);
	} else {
		__destroy_lock (lock);
	}
}


void
__grant_blocked_locks (xlator_t *this, pl_inode_t *pl_inode,
		       gf_lk_domain_t dom, struct list_head *granted)
{
	struct list_head  tmp_list;
	posix_lock_t     *l = NULL;
	posix_lock_t     *tmp = NULL;
	posix_lock_t     *conf = NULL;

	INIT_LIST_HEAD (&tmp_list);

	list_for_each_entry_safe (l, tmp, DOMAIN_HEAD (pl_inode, dom), list) {
		if (l->blocked) {
			conf = first_overlap (pl_inode, l, dom);
			if (conf)
				continue;

			l->blocked = 0;
			list_move_tail (&l->list, &tmp_list);
		}
	}

	list_for_each_entry_safe (l, tmp, &tmp_list, list) {
		list_del_init (&l->list);

		if (__is_lock_grantable (pl_inode, l, dom)) {
			conf = CALLOC (1, sizeof (*conf));

			if (!conf) {
				l->blocked = 1;
				__insert_lock (pl_inode, l, dom);
				continue;
			}

			conf->frame = l->frame;
			l->frame = NULL;

			posix_lock_to_flock (l, &conf->user_flock);

			gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => Granted",
				l->fl_type == F_UNLCK ? "Unlock" : "Lock",
				l->client_pid,
				l->user_flock.l_start,
				l->user_flock.l_len);

			__insert_and_merge (pl_inode, l, dom);

			list_add (&conf->list, granted);
		} else {
			l->blocked = 1;
			__insert_lock (pl_inode, l, dom);
		}
	}
}


void
grant_blocked_locks (xlator_t *this, pl_inode_t *pl_inode, gf_lk_domain_t dom)
{
	struct list_head granted_list;
	posix_lock_t     *tmp = NULL;
	posix_lock_t     *lock = NULL;

	INIT_LIST_HEAD (&granted_list);

	pthread_mutex_lock (&pl_inode->mutex);
	{
		__grant_blocked_locks (this, pl_inode, dom, &granted_list);
	}
	pthread_mutex_unlock (&pl_inode->mutex);

	list_for_each_entry_safe (lock, tmp, &granted_list, list) {
		list_del_init (&lock->list);

		STACK_UNWIND (lock->frame, 0, 0, &lock->user_flock);

		FREE (lock);
	}

	return;
}


int
pl_setlk (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
	  int can_block,  gf_lk_domain_t dom)
{
	int              ret = 0;

	errno = 0;

	pthread_mutex_lock (&pl_inode->mutex);
	{
		if (__is_lock_grantable (pl_inode, lock, dom)) {
			gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => OK",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len);
			__insert_and_merge (pl_inode, lock, dom);
		} else if (can_block) {
			gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => Blocked",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len);
			lock->blocked = 1;
			__insert_lock (pl_inode, lock, dom);
			ret = -1;
		} else {
			gf_log (this->name, GF_LOG_TRACE,
				"%s (pid=%d) %"PRId64" - %"PRId64" => NOK",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len);
			errno = EAGAIN;
			ret = -1;
		}
	}
	pthread_mutex_unlock (&pl_inode->mutex);

	grant_blocked_locks (this, pl_inode, dom);

	do_blocked_rw (pl_inode);

	return ret;
}


posix_lock_t *
pl_getlk (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t dom)
{
	posix_lock_t *conf = NULL;

	conf = first_overlap (pl_inode, lock, dom);

	if (conf == NULL) {
		lock->fl_type = F_UNLCK;
		return lock;
	}

	return conf;
}
