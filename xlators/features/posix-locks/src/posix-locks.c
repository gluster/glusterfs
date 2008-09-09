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
#include "posix-locks.h"


#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */

/* Forward declarations */

static posix_lock_t * delete_lock (pl_inode_t *, posix_lock_t *, gf_lk_domain_t);
static pl_rw_req_t * delete_rw_req (pl_inode_t *, pl_rw_req_t *);
static void destroy_lock (posix_lock_t *);
static void do_blocked_rw (pl_inode_t *);
static int rw_allowable (pl_inode_t *, posix_lock_t *, rw_op_t);

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

/* Insert an rw request into the inode's rw list */
static pl_rw_req_t *
insert_rw_req (pl_inode_t *inode, pl_rw_req_t *rw)
{
	rw->next = inode->rw_reqs;
	rw->prev = NULL;
	if (inode->rw_reqs)
		inode->rw_reqs->prev = rw;
	inode->rw_reqs = rw;
	return rw;
}

/* Delete an rw request from the inode's rw list */
static pl_rw_req_t *
delete_rw_req (pl_inode_t *inode, pl_rw_req_t *rw)
{
	if (rw == inode->rw_reqs) {
		inode->rw_reqs = rw->next;
		if (inode->rw_reqs)
			inode->rw_reqs->prev = NULL;
	}
	else {
		pl_rw_req_t *prev = rw->prev;
		if (prev)
			prev->next = rw->next;
		if (rw->next)
			rw->next->prev = prev;
	}

	return rw;
}

/* Create a new posix_lock_t */
static posix_lock_t *
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
static void
destroy_lock (posix_lock_t *lock)
{
	if (lock->user_flock)
		free (lock->user_flock);
	free (lock);
}

/* Convert a posix_lock to a struct flock */
static void
posix_lock_to_flock (posix_lock_t *lock, struct flock *flock)
{
	flock->l_start = lock->fl_start;
	flock->l_type  = lock->fl_type;
	flock->l_len   = lock->fl_end == LLONG_MAX ? 0 : lock->fl_end - lock->fl_start + 1;
	flock->l_pid   = lock->client_pid;
}

/* Insert the lock into the inode's lock list */

static posix_lock_t *
insert_lock (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *l, *prev;
	posix_lock_t **locks_list = (domain == GF_LOCK_POSIX ? &inode->locks
				     : &inode->internal_locks);

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
static posix_lock_t *
delete_lock (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t **locks_list = (domain == GF_LOCK_POSIX ? &inode->locks
				     : &inode->internal_locks);
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

	return lock;
}

/* Return true if the locks overlap, false otherwise */
static int
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
static int
same_owner (posix_lock_t *l1, posix_lock_t *l2)
{
	return ((l1->client_pid == l2->client_pid) &&
		(l1->transport  == l2->transport));
}

/* Delete all F_UNLCK locks */
static void
delete_unlck_locks (pl_inode_t *inode, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(inode, domain);
	while (l) {
		if (l->fl_type == F_UNLCK) {
			delete_lock (inode, l, domain);
			destroy_lock (l);
		}

		l = l->next;
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

/* {small} must always be contained inside {big} */
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
first_overlap (pl_inode_t *inode, posix_lock_t *lock,
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

static void
grant_blocked_locks (pl_inode_t *inode, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(inode, domain);

	while (l) {
		if (l->blocked) {
			posix_lock_t *conf = first_overlap (inode, l, LOCKS_FOR_DOMAIN(inode, domain));
			if (conf == NULL) {
				l->blocked = 0;
				posix_lock_to_flock (l, l->user_flock);

#ifdef _POSIX_LOCKS_DEBUG
				printf ("[UNBLOCKING] "); print_lock (l);
#endif

				STACK_UNWIND (l->frame, 0, 0, l->user_flock);
			}
		}
    
		l = l->next;
	}
}

static posix_lock_t *
posix_getlk (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *conf = first_overlap (inode, lock, LOCKS_FOR_DOMAIN(inode, domain));
	if (conf == NULL) {
		lock->fl_type = F_UNLCK;
		return lock;
	}

	return conf;
}

/* Return true if lock is grantable */
static int
lock_grantable (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(inode, domain);
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

static void
insert_and_merge (pl_inode_t *inode, posix_lock_t *lock, gf_lk_domain_t domain)
{
	posix_lock_t *conf = first_overlap (inode, lock, LOCKS_FOR_DOMAIN(inode, domain));
	while (conf) {
		if (same_owner (conf, lock)) {
			if (conf->fl_type == lock->fl_type) {
				posix_lock_t *sum = add_locks (lock, conf);
				sum->fl_type    = lock->fl_type;
				sum->transport  = lock->transport;
				sum->client_pid = lock->client_pid;

				delete_lock (inode, conf, domain); 
				destroy_lock (conf);

				destroy_lock (lock);
				insert_and_merge (inode, sum, domain);
				return;
			}
			else {
				posix_lock_t *sum = add_locks (lock, conf);
				int i;

				sum->fl_type    = conf->fl_type;
				sum->transport  = conf->transport;
				sum->client_pid = conf->client_pid;

				struct _values v = subtract_locks (sum, lock);
	
				delete_lock (inode, conf, domain);
				destroy_lock (conf);

				for (i = 0; i < 3; i++) {
					if (v.locks[i]) {
						insert_and_merge (inode, v.locks[i], domain);
					}
				}

				delete_unlck_locks (inode, domain);
				do_blocked_rw (inode);
				grant_blocked_locks (inode, domain);
				return; 
			}
		}

		if (lock->fl_type == F_UNLCK) {
			conf = first_overlap (inode, lock, conf->next);
			continue;
		}

		if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
			insert_lock (inode, lock, domain);
			return;
		}
	}

	/* no conflicts, so just insert */
	if (lock->fl_type != F_UNLCK) {
		insert_lock (inode, lock, domain);
	}
}

int
posix_setlk (pl_inode_t *inode, posix_lock_t *lock, int can_block, 
	     gf_lk_domain_t domain)
{
	errno = 0;

	if (lock_grantable (inode, lock, domain)) {
		insert_and_merge (inode, lock, domain);
	}
	else if (can_block) {
#ifdef _POSIX_LOCKS_DEBUG
		printf ("[BLOCKING]: "); print_lock (lock);
#endif
		lock->blocked = 1;
		insert_lock (inode, lock, domain);
		return -1;
	}
	else {
		errno = EAGAIN;
		return -1;
	}

	return 0;
}

/* fops */

struct _truncate_ops {
	void *loc_or_fd;
	off_t offset;
	enum {TRUNCATE, FTRUNCATE} op;
};

int32_t
pl_truncate_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 struct stat *buf)
{
	struct _truncate_ops *local = (struct _truncate_ops *) frame->local;
	if (local) {
		if (local->loc_or_fd && local->op == TRUNCATE) {
			FREE (((loc_t *)local->loc_or_fd)->path);
			FREE (local->loc_or_fd);
		}
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int 
truncate_allowed (pl_inode_t *inode, 
		  transport_t *transport, pid_t client_pid, 
		  off_t offset)
{
	posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
	ERR_ABORT (region);
	region->fl_start = offset;
	region->fl_end   = LLONG_MAX;
	region->transport = transport;
	region->client_pid = client_pid;

	posix_lock_t *l = inode->locks;
	while (l) {
		if (!l->blocked && locks_overlap (region, l) &&
		    !same_owner (region, l)) {
			free (region);
			return 0;
		}
		l = l->next;
	}

	free (region);
	return 1;
}

static int32_t
truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	struct _truncate_ops *local = (struct _truncate_ops *)frame->local;
	dict_t *inode_ctx;

	if (op_ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, 
			"got error (errno=%d, stderror=%s) from child", 
			op_errno, strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, buf);
		return 0;
	}

	if (local->op == TRUNCATE)
		inode_ctx = ((loc_t *)local->loc_or_fd)->inode->ctx;
	else
		inode_ctx = ((fd_t *)local->loc_or_fd)->inode->ctx;

	data_t *inode_data = dict_get (inode_ctx, this->name);
	pl_inode_t *inode;
	if (inode_data == NULL) {
		mode_t st_mode;
		inode = calloc (1, sizeof (pl_inode_t));
		ERR_ABORT (inode);

		if (local->op == TRUNCATE)
			st_mode = ((loc_t *)local->loc_or_fd)->inode->st_mode;
		else
			st_mode = ((fd_t *)local->loc_or_fd)->inode->st_mode;

		if ((st_mode & S_ISGID) && !(st_mode & S_IXGRP))
			inode->mandatory = 1;

		dict_set (inode_ctx, this->name, bin_to_data (inode, sizeof (inode)));
	}
	else {
		inode = (pl_inode_t *)data_to_bin (inode_data);
	}

	if (inode && priv->mandatory && inode->mandatory &&
	    !truncate_allowed (inode, frame->root->trans,
			       frame->root->pid, local->offset)) {
		gf_log (this->name, GF_LOG_ERROR, "returning EAGAIN");    
		STACK_UNWIND (frame, -1, EAGAIN, buf);
		return 0;
	}

	switch (local->op) {
	case TRUNCATE:
		STACK_WIND (frame, pl_truncate_cbk,
			    FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
			    (loc_t *)local->loc_or_fd, local->offset);
		break;
	case FTRUNCATE:
		STACK_WIND (frame, pl_truncate_cbk,
			    FIRST_CHILD (this), FIRST_CHILD (this)->fops->ftruncate,
			    (fd_t *)local->loc_or_fd, local->offset);
		break;
	}

	return 0;
}


int32_t 
pl_truncate (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, off_t offset)
{
	GF_ERROR_IF_NULL (this);

	struct _truncate_ops *local = calloc (1, sizeof (struct _truncate_ops));
	ERR_ABORT (local);
	local->loc_or_fd  = memdup (loc, sizeof (*loc));
	((loc_t *)(local->loc_or_fd))->path = strdup (loc->path);
	local->offset     = offset;
	local->op         = TRUNCATE;

	frame->local = local;

	STACK_WIND (frame, truncate_stat_cbk, 
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->stat,
		    loc);

	return 0;
}


int32_t 
pl_ftruncate (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, off_t offset)
{
	struct _truncate_ops *local = calloc (1, sizeof (struct _truncate_ops));
	ERR_ABORT (local);

	local->loc_or_fd   = fd;
	local->offset      = offset;
	local->op          = FTRUNCATE;

	frame->local = local;

	STACK_WIND (frame, truncate_stat_cbk, 
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat, 
		    fd);
	return 0;
}


int32_t 
pl_close_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno)
{
	GF_ERROR_IF_NULL (this);

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


static void
delete_locks_of_owner (pl_inode_t *inode, transport_t *transport,
		       pid_t pid, gf_lk_domain_t domain)
{
	posix_lock_t *l = LOCKS_FOR_DOMAIN(inode, domain);
	while (l) {
		posix_lock_t *tmp = l;
		l = l->next;
		if ((tmp->transport == transport) && 
		    (tmp->client_pid == pid)) {
			delete_lock (inode, tmp, domain);
			destroy_lock (tmp);
		}
	}
}


int32_t 
pl_close (call_frame_t *frame, xlator_t *this,
	  fd_t *fd)
{
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (fd);

	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	pthread_mutex_lock (&priv->mutex);

	struct flock nulllock = {0, };
	data_t *fd_data = dict_get (fd->ctx, this->name);
	if (fd_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}
	pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}
	pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

	dict_del (fd->ctx, this->name);

	delete_locks_of_owner (inode, frame->root->trans, frame->root->pid, 
			       GF_LOCK_POSIX);
	delete_locks_of_owner (inode, frame->root->trans, frame->root->pid, 
			       GF_LOCK_INTERNAL);

	do_blocked_rw (inode);

	grant_blocked_locks (inode, GF_LOCK_POSIX);
	grant_blocked_locks (inode, GF_LOCK_INTERNAL);

	free (pfd);

	pthread_mutex_unlock (&priv->mutex);

	STACK_WIND (frame, pl_close_cbk, 
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->close, 
		    fd);
	return 0;
}


int32_t 
pl_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t 
pl_flush (call_frame_t *frame, xlator_t *this,
	  fd_t *fd)
{
	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF);
		return 0;
	}
	pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

	delete_locks_of_owner (inode, frame->root->trans, frame->root->pid, GF_LOCK_POSIX);
	delete_locks_of_owner (inode, frame->root->trans, frame->root->pid, GF_LOCK_INTERNAL);

	do_blocked_rw (inode);

	grant_blocked_locks (inode, GF_LOCK_POSIX);
	grant_blocked_locks (inode, GF_LOCK_INTERNAL);

	STACK_WIND (frame, pl_flush_cbk, 
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->flush, 
		    fd);
	return 0;
}

struct _flags {
	int32_t flags;
};

int32_t 
pl_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	GF_ERROR_IF_NULL (frame);
	GF_ERROR_IF_NULL (this);
	GF_ERROR_NO_RETURN_IF_NULL (fd);

	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	pthread_mutex_lock (&priv->mutex);

	if (op_ret >= 0) {
		pl_fd_t *pfd = NULL;
		pl_inode_t *inode;
    
		pfd = calloc (1, sizeof (pl_fd_t));
		ERR_ABORT (pfd);

		struct _flags *local = frame->local;
		if (frame->local)
			pfd->nonblocking = local->flags & O_NONBLOCK;

		if (!fd->inode) {
			gf_log (this->name, GF_LOG_ERROR, "fd->inode is NULL! returning EBADFD");
			STACK_UNWIND (frame, -1, EBADFD, fd);
		}

		data_t *inode_data = dict_get (fd->inode->ctx, this->name);
		if (inode_data == NULL) {
			pl_inode_t *inode = calloc (1, sizeof (pl_inode_t));
			ERR_ABORT (inode);

			mode_t st_mode = fd->inode->st_mode;
			if ((st_mode & S_ISGID) && !(st_mode & S_IXGRP))
				inode->mandatory = 1;

			dict_set (fd->inode->ctx, this->name, bin_to_data (inode, sizeof (inode)));
		}
		else {
			inode = data_to_bin (inode_data);
		}
    
		dict_set (fd->ctx, this->name, bin_to_data (pfd, sizeof (pfd)));
	}

	pthread_mutex_unlock (&priv->mutex);

	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}

int32_t 
pl_open (call_frame_t *frame, xlator_t *this,
	 loc_t *loc, int32_t flags, fd_t *fd)
{
	GF_ERROR_IF_NULL (frame);
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (loc);

	struct _flags *f = calloc (1, sizeof (struct _flags));
	ERR_ABORT (f);
	f->flags = flags;

	if (flags & O_RDONLY)
		f->flags &= ~O_TRUNC;

	frame->local = f;

	STACK_WIND (frame, pl_open_cbk, 
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open, 
		    loc, flags & ~O_TRUNC, fd);

	return 0;
}

int32_t
pl_create_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int32_t op_ret, int32_t op_errno,
	       fd_t *fd, inode_t *inode, struct stat *buf)
{
	if (op_ret >= 0) {
		pl_inode_t *pinode = NULL;
		pl_fd_t *pfd = NULL;

		pinode = calloc (1, sizeof (pl_inode_t));
		ERR_ABORT (pinode);
		pfd = calloc (1, sizeof (pl_fd_t));
		ERR_ABORT (pfd);
    
		dict_set (fd->inode->ctx, this->name, bin_to_data (pinode, sizeof (pinode)));
		dict_set (fd->ctx, this->name, bin_to_data (pfd, sizeof (pfd)));
	}
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t 
pl_create (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	GF_ERROR_IF_NULL (frame);
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (loc->path);

	STACK_WIND (frame, pl_create_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->create, 
		    loc, flags, mode, fd);
	return 0;
}

int32_t
pl_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno,
	      struct iovec *vector, int32_t count, struct stat *stbuf)
{
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (vector);

	STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
	return 0;
}

int32_t
pl_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
	       int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	GF_ERROR_IF_NULL (this);

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}

static void
do_blocked_rw (pl_inode_t *inode)
{
	pl_rw_req_t *rw = inode->rw_reqs;

	while (rw) {
		if (rw_allowable (inode, rw->region, rw->op)) {
			switch (rw->op) {
			case OP_READ:
				STACK_WIND (rw->frame, pl_readv_cbk,
					    FIRST_CHILD (rw->this), FIRST_CHILD (rw->this)->fops->readv,
					    rw->fd, rw->size, rw->region->fl_start);
				break;
			case OP_WRITE: 
			{
				dict_t *req_refs = rw->frame->root->req_refs;
				STACK_WIND (rw->frame, pl_writev_cbk,
					    FIRST_CHILD (rw->this), 
					    FIRST_CHILD (rw->this)->fops->writev,
					    rw->fd, rw->vector, rw->size, rw->region->fl_start);
				dict_unref (req_refs);
				break;
			}
			}
      
			delete_rw_req (inode, rw);
			free (rw);
		}
    
		rw = rw->next;
	}
}

static int
rw_allowable (pl_inode_t *inode, posix_lock_t *region,
	      rw_op_t op)
{
	posix_lock_t *l = inode->locks;
	while (l) {
		if (locks_overlap (l, region) && !same_owner (l, region)) {
			if ((op == OP_READ) && (l->fl_type != F_WRLCK))
				continue;
			return 0;
		}
		l = l->next;
	}

	return 1;
}

int32_t
pl_readv (call_frame_t *frame, xlator_t *this,
	  fd_t *fd, size_t size, off_t offset)
{
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (fd);

	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	struct stat nullbuf = {0, };

	pthread_mutex_lock (&priv->mutex);

	data_t *fd_data = dict_get (fd->ctx, this->name);
	if (fd_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nullbuf);
		return 0;
	}
	pl_fd_t *pfd = (pl_fd_t *) data_to_bin (fd_data);

	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nullbuf);
		return 0;
	}
	pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

	if (priv->mandatory && inode->mandatory) {
		posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (region);
		region->fl_start = offset;
		region->fl_end   = offset + size - 1;
		region->transport = frame->root->trans;
		region->client_pid = frame->root->pid;
    
		if (!rw_allowable (inode, region, OP_READ)) {
			if (pfd->nonblocking) {
				pthread_mutex_unlock (&priv->mutex);
				gf_log (this->name, GF_LOG_ERROR, "returning EWOULDBLOCK");
				STACK_UNWIND (frame, -1, EWOULDBLOCK, &nullbuf);
				return -1;
			}

			pl_rw_req_t *rw = calloc (1, sizeof (pl_rw_req_t));
			ERR_ABORT (rw);
			rw->frame  = frame;
			rw->this   = this;
			rw->fd     = fd;
			rw->op     = OP_READ;
			rw->size   = size;
			rw->region = region;

			insert_rw_req (inode, rw);
			pthread_mutex_unlock (&priv->mutex);
			return 0;
		}
	}

	pthread_mutex_unlock (&priv->mutex);

	STACK_WIND (frame, pl_readv_cbk, 
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
		    fd, size, offset);
	return 0;
}

static int32_t
iovec_total_length (struct iovec *vector, int count)
{
	int32_t i;
	int32_t total_length = 0;
	for (i = 0; i < count; i++) {
		total_length += vector[i].iov_len;
	}

	return total_length;
}

int32_t 
pl_writev (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, struct iovec *vector, int32_t count, off_t offset)
{
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (fd);
	GF_ERROR_IF_NULL (vector);

	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	struct stat nullbuf = {0, };

	pthread_mutex_lock (&priv->mutex);

	data_t *fd_data = dict_get (fd->ctx, this->name);
	if (fd_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nullbuf);
		return 0;
	}
	pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nullbuf);
		return 0;
	}
	pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

	if (priv->mandatory && inode->mandatory) {
		int size = iovec_total_length (vector, count);

		posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (region);
		region->fl_start = offset;
		region->fl_end   = offset + size - 1;
		region->transport = frame->root->trans;
		region->client_pid = frame->root->pid;

		if (!rw_allowable (inode, region, OP_WRITE)) {
			if (pfd->nonblocking) {
				pthread_mutex_unlock (&priv->mutex);
				gf_log (this->name, GF_LOG_ERROR, "returning EWOULDBLOCK");
				STACK_UNWIND (frame, -1, EWOULDBLOCK, &nullbuf);
				return -1;
			}

			pl_rw_req_t *rw = calloc (1, sizeof (pl_rw_req_t));
			ERR_ABORT (rw);

			dict_ref (frame->root->req_refs);
			rw->frame  = frame;
			rw->this   = this;
			rw->fd     = fd;
			rw->op     = OP_WRITE;
			rw->size   = count;
			rw->vector = iov_dup (vector, count);
			rw->region = region;

			insert_rw_req (inode, rw);
			pthread_mutex_unlock (&priv->mutex);
			return 0;
		}
	}

	pthread_mutex_unlock (&priv->mutex);

	STACK_WIND (frame, pl_writev_cbk,
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev, 
		    fd, vector, count, offset);
	return 0;
}

int32_t
pl_lk_common (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t cmd,
	      struct flock *flock, gf_lk_domain_t domain)
{
	GF_ERROR_IF_NULL (frame);
	GF_ERROR_IF_NULL (this);
	GF_ERROR_IF_NULL (fd);
	GF_ERROR_IF_NULL (flock);

	transport_t *transport = frame->root->trans;
	pid_t client_pid = frame->root->pid;
	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	pthread_mutex_lock (&priv->mutex);

	struct flock nulllock = {0, };
	data_t *fd_data = dict_get (fd->ctx, this->name);
	if (fd_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}
	pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

	if (!pfd) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, nulllock);
		return -1;
	}

	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}
	pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);
  
	posix_lock_t *reqlock = new_posix_lock (flock, transport, client_pid);

	int can_block = 0;

	switch (cmd) {
#if F_GETLK != F_GETLK64
	case F_GETLK64:
#endif

	case F_GETLK: {
		posix_lock_t *conf = posix_getlk (inode, reqlock, domain);
		posix_lock_to_flock (conf, flock);
		pthread_mutex_unlock (&priv->mutex);
		destroy_lock (reqlock);

#ifdef _POSIX_LOCKS_DEBUG
		printf ("[GET] "); print_lock (reqlock);
#endif

		STACK_UNWIND (frame, 0, 0, flock);
		return 0;
	}

#if F_SETLKW != F_SETLKW64
	case F_SETLKW64:
#endif
	case F_SETLKW:
		can_block = 1;
		reqlock->frame = frame;
		reqlock->this  = this;
		reqlock->fd    = fd;
		reqlock->user_flock = calloc (1, sizeof (struct flock));
		ERR_ABORT (reqlock->user_flock);
		memcpy (reqlock->user_flock, flock, sizeof (struct flock));

#if F_SETLK != F_SETLK64
	case F_SETLK64:
#endif
	case F_SETLK: {
		int ret = posix_setlk (inode, reqlock, can_block, domain);

#ifdef _POSIX_LOCKS_DEBUG
		printf ("[SET] (ret=%d)", ret); print_lock (reqlock);
#endif

		pthread_mutex_unlock (&priv->mutex);

		if (ret == -1) {
			if (can_block)
				return -1;

			gf_log (this->name, GF_LOG_ERROR, "returning EAGAIN");
			STACK_UNWIND (frame, ret, EAGAIN, flock);
			return -1;
		}

		if (ret == 0) {
			STACK_UNWIND (frame, ret, 0, flock);
			return 0;
		}
	}
	}

	pthread_mutex_unlock (&priv->mutex);
	gf_log (this->name, GF_LOG_ERROR, "returning EINVAL");
	STACK_UNWIND (frame, -1, EINVAL, flock); /* Normally this shouldn't be reached */
	return -1;
}

int32_t
pl_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd,
       struct flock *flock) 
{
	return pl_lk_common (frame, this, fd, cmd, flock, GF_LOCK_POSIX);
}

/*
 * This fop provides fcntl-style locking for internal purposes. Locks
 * held through this fop reside in a domain different from those held
 * by applications. This fop is for the use of AFR.
 */

int32_t
pl_gf_lk (call_frame_t *frame, xlator_t *this,
	  fd_t *fd, int32_t cmd,
	  struct flock *flock)
{
	return pl_lk_common (frame, this, fd, cmd, flock, GF_LOCK_INTERNAL);
}

int32_t
pl_forget (call_frame_t *frame,
	   xlator_t *this,
	   inode_t *inode)
{
	data_t *inode_data = dict_get (inode->ctx, this->name);
	if (inode_data) {
		pl_inode_t *pl_inode = (pl_inode_t *)data_to_bin (inode_data);
		if (pl_inode->rw_reqs) {
			gf_log (this->name, GF_LOG_ERROR,
				"Pending R/W requests found!");
		}
		if (pl_inode->locks) {
			gf_log (this->name, GF_LOG_ERROR,
				"Active locks found!");
		}

		free (pl_inode);
	}
	return 0;
}

int32_t
init (xlator_t *this)
{
	posix_locks_private_t *priv = NULL;
	xlator_list_t *trav = NULL;
	data_t *mandatory = NULL;
	if (!this->children) {
		gf_log (this->name, 
			GF_LOG_ERROR, 
			"FATAL: posix-locks should have exactly one child");
		return -1;
	}

	if (this->children->next) {
		gf_log (this->name, 
			GF_LOG_ERROR, 
			"FATAL: posix-locks should have exactly one child");
		return -1;
	}

	trav = this->children;
	while (trav->xlator->children) trav = trav->xlator->children;

	if (strncmp ("storage/", trav->xlator->type, 8)) {
		gf_log (this->name, GF_LOG_ERROR,
			"'posix-locks' not loaded over storage translator");
		return -1;
	}

	priv = calloc (1, sizeof (posix_locks_private_t));
	ERR_ABORT (priv);
	pthread_mutex_init (&priv->mutex, NULL);

	mandatory = dict_get (this->options, "mandatory");
	if (mandatory) {
		if (strcasecmp (mandatory->data, "on") == 0)
			priv->mandatory = 1;
	}

	this->private = priv;
	return 0;
}

int32_t
fini (xlator_t *this)
{
	posix_locks_private_t *priv = this->private;
	free (priv);
	return 0;
}

struct xlator_fops fops = {
	.create      = pl_create,
	.truncate    = pl_truncate,
	.ftruncate   = pl_ftruncate,
	.open        = pl_open,
	.readv       = pl_readv,
	.writev      = pl_writev,
	.close       = pl_close,
	.lk          = pl_lk,
	.gf_lk       = pl_gf_lk,
	.flush       = pl_flush,
	.forget      = pl_forget
};

struct xlator_mops mops = {
};

struct xlator_options options[] = {
	{ "mandatory", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0 },
};
