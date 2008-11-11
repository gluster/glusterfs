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


#ifdef _POSIX_LOCKS_DEBUG
static void
print_lock (posix_lock_t *lock)
{
	switch (lock->fl_type) {
	case F_RDLCK:
		printf ("READ");
		break;
	case F_WRLCK:
		printf ("WRITE");
		break;
	case F_UNLCK:
		printf ("UNLOCK");
		break;
	}
  
	printf (" (%u, ", lock->fl_start);
	printf ("%u), ", lock->fl_end);
	printf ("pid = %lu\n", lock->client_pid); 
	fflush (stdout);
}

static void
print_flock (struct flock *lock)
{
	switch (lock->l_type) {
	case F_RDLCK:
		printf ("READ");
		break;
	case F_WRLCK:
		printf ("WRITE");
		break;
	case F_UNLCK:
		printf ("UNLOCK");
		break;
	}
  
	printf (" (%u, ", lock->l_start);
	printf ("%u), ", lock->l_start+lock->l_len);
	printf ("pid = %lu\n", lock->l_pid); 
	fflush (stdout);
}
#endif /* _POSIX_LOCKS_DEBUG */


/* Create a new posix_lock_t */
posix_lock_t *
new_posix_lock (struct flock *flock, transport_t *transport, pid_t client_pid)
{
	posix_lock_t *lock = (posix_lock_t *)calloc (1, sizeof (posix_lock_t));
	ERR_ABORT (lock);

	lock->fl_start = flock->l_start;
	lock->fl_type = flock->l_type;

	if (flock->l_len == 0)
		lock->fl_end = LLONG_MAX;
	else
		lock->fl_end = flock->l_start + flock->l_len - 1;

	lock->transport  = transport;
	lock->client_pid = client_pid;
	return lock;
}

/* Destroy a posix_lock */
void
destroy_lock (posix_lock_t *lock)
{
	free (lock);
}

/* Convert a posix_lock to a struct flock */
void
posix_lock_to_flock (posix_lock_t *lock, struct flock *flock)
{
	flock->l_start = lock->fl_start;
	flock->l_type  = lock->fl_type;
	flock->l_len   = lock->fl_end == LLONG_MAX ? 0 : lock->fl_end - lock->fl_start + 1;
	flock->l_pid   = lock->client_pid;
}

/* Insert the lock into the inode's lock list */

static posix_lock_t *
insert_lock (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *l, *prev;
	posix_lock_t **locks_list = (domain == GF_LOCK_POSIX ? &pl_inode->posix_locks
				     : &pl_inode->gf_file_locks);

	if (*locks_list) {
		prev = *locks_list;
		l = prev->next;
		while (l) {
			prev = l;
			l = l->next;
		}

		prev->next = lock;
		lock->prev = prev;
		lock->next = NULL;
	}
	else {
		*locks_list = lock;
		lock->prev = NULL;
		lock->next = NULL;
	}

	return lock;
}

/* Delete a lock from the inode's lock list */
posix_lock_t *
delete_lock (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t **locks_list = (domain == GF_LOCK_POSIX ? &pl_inode->posix_locks
				     : &pl_inode->gf_file_locks);
	if (lock == *locks_list) {
		*locks_list = lock->next;
		if (*locks_list)
			(*locks_list)->prev = NULL;
	}
	else {
		posix_lock_t *prev = lock->prev;
		if (prev)
			prev->next = lock->next;
		if (lock->next)
			lock->next->prev = prev;
	}

	lock->prev = NULL;
	lock->next = NULL;

	return lock;
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
delete_unlck_locks (pl_inode_t *pl_inode, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(pl_inode, domain);
	posix_lock_t *l_tmp = NULL;

	while (l) {
		l_tmp = l->next;

		if (l->fl_type == F_UNLCK) {
			delete_lock (pl_inode, l, domain);
			destroy_lock (l);
		}
		l = l_tmp;
	}
}

/* Add two locks */
static posix_lock_t *
add_locks (posix_lock_t *l1, posix_lock_t *l2)
{
	posix_lock_t *sum = calloc (1, sizeof (posix_lock_t));
	ERR_ABORT (sum);
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
		v.locks[0] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_type = small->fl_type;
	}
	else if ((small->fl_start > big->fl_start) &&
		 (small->fl_end   < big->fl_end)) {
		/* both edges lie inside big */
		v.locks[0] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);
		v.locks[2] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[2]);

		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_end = small->fl_start - 1;

		memcpy (v.locks[1], small, sizeof (posix_lock_t));
		memcpy (v.locks[2], big, sizeof (posix_lock_t));
		v.locks[2]->fl_start = small->fl_end + 1;
	}
	/* one edge coincides with big */
	else if (small->fl_start == big->fl_start) {
		v.locks[0] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);
    
		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_start = small->fl_end + 1;
    
		memcpy (v.locks[1], small, sizeof (posix_lock_t));
	}
	else if (small->fl_end   == big->fl_end) {
		v.locks[0] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[0]);
		v.locks[1] = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (v.locks[1]);

		memcpy (v.locks[0], big, sizeof (posix_lock_t));
		v.locks[0]->fl_end = small->fl_start - 1;
    
		memcpy (v.locks[1], small, sizeof (posix_lock_t));
	}
	else {
		gf_log ("posix-locks", GF_LOG_DEBUG, 
			"unexpected case in subtract_locks");
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
	       posix_lock_t *begin)
{
	posix_lock_t *l;
	if (!begin)
		return NULL;

	l = begin;
	while (l) {
		if (l->blocked) {
			l = l->next;
			continue;
		}

		if (locks_overlap (l, lock))
			return l;

		l = l->next;
	}

	return NULL;
}

void
grant_blocked_locks (pl_inode_t *pl_inode, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(pl_inode, domain);

	while (l) {
		if (l->blocked) {
			posix_lock_t *conf = first_overlap (pl_inode, l, LOCKS_FOR_DOMAIN(pl_inode, domain));
			if (conf == NULL) {
				l->blocked = 0;
				posix_lock_to_flock (l, &l->user_flock);

#ifdef _POSIX_LOCKS_DEBUG
				printf ("[UNBLOCKING] "); print_lock (l);
#endif

				STACK_UNWIND (l->frame, 0, 0, &l->user_flock);
			}
		}
    
		l = l->next;
	}
}

posix_lock_t *
pl_getlk (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *conf = first_overlap (pl_inode, lock, LOCKS_FOR_DOMAIN(pl_inode, domain));
	if (conf == NULL) {
		lock->fl_type = F_UNLCK;
		return lock;
	}

	return conf;
}

/* Return true if lock is grantable */
static int
lock_grantable (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(pl_inode, domain);
	while (l) {
		if (!l->blocked && locks_overlap (lock, l)) {
			if (((l->fl_type    == F_WRLCK) || (lock->fl_type == F_WRLCK)) &&
			    (lock->fl_type != F_UNLCK) && 
			    !same_owner (l, lock)) {
				return 0;
			}
		}
		l = l->next;
	}
	return 1;
}

extern void do_blocked_rw (pl_inode_t *);

static void
insert_and_merge (pl_inode_t *pl_inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *conf = first_overlap (pl_inode, lock, LOCKS_FOR_DOMAIN(pl_inode, domain));
	while (conf) {
		if (same_owner (conf, lock)) {
			if (conf->fl_type == lock->fl_type) {
				posix_lock_t *sum = add_locks (lock, conf);
				sum->fl_type    = lock->fl_type;
				sum->transport  = lock->transport;
				sum->client_pid = lock->client_pid;

				delete_lock (pl_inode, conf, domain); 
				destroy_lock (conf);

				destroy_lock (lock);
				insert_and_merge (pl_inode, sum, domain);

				return;
			}
			else {
				posix_lock_t *sum = add_locks (lock, conf);
				int i;

				sum->fl_type    = conf->fl_type;
				sum->transport  = conf->transport;
				sum->client_pid = conf->client_pid;

				struct _values v = subtract_locks (sum, lock);
	
				delete_lock (pl_inode, conf, domain);
				destroy_lock (conf);

				delete_lock (pl_inode, lock, domain);
				destroy_lock (lock);

				destroy_lock (sum);

				for (i = 0; i < 3; i++) {
					if (v.locks[i]) {
						if (v.locks[i]->fl_type == F_UNLCK) {
							destroy_lock (v.locks[i]);
							continue;
						}
						insert_and_merge (pl_inode, v.locks[i], domain);
					}
				}

				delete_unlck_locks (pl_inode, domain);
				do_blocked_rw (pl_inode);
				grant_blocked_locks (pl_inode, domain);
				return; 
			}
		}

		if (lock->fl_type == F_UNLCK) {
			conf = first_overlap (pl_inode, lock, conf->next);
			continue;
		}

		if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
			insert_lock (pl_inode, lock, domain);
			return;
		}
	}

	/* no conflicts, so just insert */
	if (lock->fl_type != F_UNLCK) {
		insert_lock (pl_inode, lock, domain);
	} else {
		destroy_lock (lock);
	}
}

int
pl_setlk (pl_inode_t *pl_inode, posix_lock_t *lock, int can_block, 
	  gf_lk_domain_t domain)
{
	errno = 0;

	if (lock_grantable (pl_inode, lock, domain)) {
		insert_and_merge (pl_inode, lock, domain);
	}
	else if (can_block) {
#ifdef _POSIX_LOCKS_DEBUG
		printf ("[BLOCKING]: "); print_lock (lock);
#endif
		lock->blocked = 1;
		insert_lock (pl_inode, lock, domain);
		return -1;
	}
	else {
		errno = EAGAIN;
		return -1;
	}

	return 0;
}


