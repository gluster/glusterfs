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
#include "common.h"

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */

/* Forward declarations */


static pl_rw_req_t * delete_rw_req (pl_inode_t *, pl_rw_req_t *);
void do_blocked_rw (pl_inode_t *);
static int rw_allowable (pl_inode_t *, posix_lock_t *, rw_op_t);

struct _truncate_ops {
	void *loc_or_fd;
	off_t offset;
	enum {TRUNCATE, FTRUNCATE} op;
};

/* Insert an rw request into the inode's rw list */
static pl_rw_req_t *
insert_rw_req (pl_inode_t *pl_inode, pl_rw_req_t *rw)
{
	rw->next = pl_inode->rw_reqs;
	rw->prev = NULL;
	if (pl_inode->rw_reqs)
		pl_inode->rw_reqs->prev = rw;
	pl_inode->rw_reqs = rw;
	return rw;
}

/* Delete an rw request from the inode's rw list */
static pl_rw_req_t *
delete_rw_req (pl_inode_t *pl_inode, pl_rw_req_t *rw)
{
	pl_rw_req_t *prev = NULL;
	if (rw == pl_inode->rw_reqs) {
		pl_inode->rw_reqs = rw->next;
		if (pl_inode->rw_reqs)
			pl_inode->rw_reqs->prev = NULL;
	}
	else {
		prev = rw->prev;
		if (prev)
			prev->next = rw->next;
		if (rw->next)
			rw->next->prev = prev;
	}

	return rw;
}

int32_t
pl_truncate_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 struct stat *buf)
{
	struct _truncate_ops *local = frame->local;
	if (local) {
		if (local->loc_or_fd && local->op == TRUNCATE) {
			FREE (((loc_t *)local->loc_or_fd)->path);
			FREE (local->loc_or_fd);
		}
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

/* TODO: why an alloc here ? can't we use stack? that would be better option */
static int 
truncate_allowed (pl_inode_t *pl_inode, 
		  transport_t *transport, pid_t client_pid, 
		  off_t offset)
{
	posix_lock_t *l = NULL;
	posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
	ERR_ABORT (region);
	region->fl_start = offset;
	region->fl_end   = LLONG_MAX;
	region->transport = transport;
	region->client_pid = client_pid;

	l = pl_inode->fcntl_locks;

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
	posix_locks_private_t *priv = this->private;
	struct _truncate_ops *local = frame->local;
	dict_t *inode_ctx = NULL;
	data_t *inode_data = NULL;
	pl_inode_t *pl_inode = NULL;

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

	inode_data = dict_get (inode_ctx, this->name);
	if (inode_data == NULL) {
		mode_t st_mode;
		pl_inode = calloc (1, sizeof (pl_inode_t));
		ERR_ABORT (pl_inode);

		if (local->op == TRUNCATE)
			st_mode = ((loc_t *)local->loc_or_fd)->inode->st_mode;
		else
			st_mode = ((fd_t *)local->loc_or_fd)->inode->st_mode;

		if ((st_mode & S_ISGID) && !(st_mode & S_IXGRP))
			pl_inode->mandatory = 1;

		dict_set (inode_ctx, this->name, data_from_ptr (pl_inode));
	}
	else {
		pl_inode = (pl_inode_t *)data_to_bin (inode_data);
	}

	if (pl_inode && priv->mandatory && pl_inode->mandatory &&
	    !truncate_allowed (pl_inode, frame->root->trans,
			       frame->root->pid, local->offset)) {
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



static void
delete_locks_of_owner (pl_inode_t *pl_inode, transport_t *transport,
		       pid_t pid, gf_lk_domain_t domain)
{
	posix_lock_t *tmp = NULL;
	posix_lock_t *l = LOCKS_FOR_DOMAIN(pl_inode, domain);
	while (l) {
		tmp = l;
		l = l->next;
		if ((tmp->transport == transport) && 
		    (tmp->client_pid == pid)) {
			delete_lock (pl_inode, tmp, domain);
			destroy_lock (tmp);
		}
	}
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
	posix_locks_private_t *priv = this->private;
	pl_inode_t *pl_inode = NULL;
	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF);
		return 0;
	}

	pthread_mutex_lock (&priv->mutex);

	pl_inode = (pl_inode_t *)data_to_bin (inode_data);
	
	delete_locks_of_owner (pl_inode, frame->root->trans, frame->root->pid, GF_LOCK_POSIX);
	delete_locks_of_owner (pl_inode, frame->root->trans, frame->root->pid, GF_LOCK_INTERNAL);

	do_blocked_rw (pl_inode);

	grant_blocked_locks (pl_inode, GF_LOCK_POSIX);
	grant_blocked_locks (pl_inode, GF_LOCK_INTERNAL);

	pthread_mutex_unlock (&priv->mutex);

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

	pl_fd_t *pfd = NULL;
	pl_inode_t *pl_inode = NULL;
	posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
	pthread_mutex_lock (&priv->mutex);

	if (op_ret >= 0) {
		pfd = calloc (1, sizeof (pl_fd_t));
		ERR_ABORT (pfd);

		struct _flags *local = frame->local;
		if (frame->local)
			pfd->nonblocking = local->flags & O_NONBLOCK;

		if (!fd->inode) {
			gf_log (this->name, GF_LOG_ERROR, "fd->inode is NULL! returning EBADFD");
			pthread_mutex_unlock (&priv->mutex);
			STACK_UNWIND (frame, -1, EBADFD, fd);
			return 0;
		}

		data_t *inode_data = dict_get (fd->inode->ctx, this->name);
		if (inode_data == NULL) {
			pl_inode = calloc (1, sizeof (pl_inode_t));
			ERR_ABORT (pl_inode);

			mode_t st_mode = fd->inode->st_mode;
			if ((st_mode & S_ISGID) && !(st_mode & S_IXGRP))
				pl_inode->mandatory = 1;

			dict_set (fd->inode->ctx, this->name, data_from_ptr (pl_inode));
		} 
    
		dict_set (fd->ctx, this->name, data_from_ptr (pfd));
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
    
		dict_set (fd->inode->ctx, this->name, data_from_ptr (pinode));
		dict_set (fd->ctx, this->name, data_from_ptr (pfd));
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

void
do_blocked_rw (pl_inode_t *pl_inode)
{
	pl_rw_req_t *rw = pl_inode->rw_reqs;

	while (rw) {
		if (rw_allowable (pl_inode, rw->region, rw->op)) {
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
      
			delete_rw_req (pl_inode, rw);
			free (rw);
		}
    
		rw = rw->next;
	}
}

static int
rw_allowable (pl_inode_t *pl_inode, posix_lock_t *region,
	      rw_op_t op)
{
	posix_lock_t *l = pl_inode->fcntl_locks;
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
	pl_inode_t *pl_inode = (pl_inode_t *)data_to_bin (inode_data);

	if (priv->mandatory && pl_inode->mandatory) {
		posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (region);
		region->fl_start = offset;
		region->fl_end   = offset + size - 1;
		region->transport = frame->root->trans;
		region->client_pid = frame->root->pid;
    
		if (!rw_allowable (pl_inode, region, OP_READ)) {
			if (pfd->nonblocking) {
				pthread_mutex_unlock (&priv->mutex);
				gf_log (this->name, GF_LOG_ERROR, "returning EWOULDBLOCK");
				STACK_UNWIND (frame, -1, EWOULDBLOCK, &nullbuf);
				return 0;
			}

			pl_rw_req_t *rw = calloc (1, sizeof (pl_rw_req_t));
			ERR_ABORT (rw);
			rw->frame  = frame;
			rw->this   = this;
			rw->fd     = fd;
			rw->op     = OP_READ;
			rw->size   = size;
			rw->region = region;

			insert_rw_req (pl_inode, rw);
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
	pl_inode_t *pl_inode = (pl_inode_t *)data_to_bin (inode_data);

	if (priv->mandatory && pl_inode->mandatory) {
		int size = iovec_total_length (vector, count);

		posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
		ERR_ABORT (region);
		region->fl_start = offset;
		region->fl_end   = offset + size - 1;
		region->transport = frame->root->trans;
		region->client_pid = frame->root->pid;

		if (!rw_allowable (pl_inode, region, OP_WRITE)) {
			if (pfd->nonblocking) {
				pthread_mutex_unlock (&priv->mutex);
				gf_log (this->name, GF_LOG_ERROR, "returning EWOULDBLOCK");
				STACK_UNWIND (frame, -1, EWOULDBLOCK, &nullbuf);
				return 0;
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

			insert_rw_req (pl_inode, rw);
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
pl_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd,
       struct flock *flock)
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
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}

	data_t *inode_data = dict_get (fd->inode->ctx, this->name);
	if (inode_data == NULL) {
		pthread_mutex_unlock (&priv->mutex);
		gf_log (this->name, GF_LOG_ERROR, "returning EBADF");
		STACK_UNWIND (frame, -1, EBADF, &nulllock);
		return 0;
	}
	pl_inode_t *pl_inode = (pl_inode_t *)data_to_bin (inode_data);
  
	posix_lock_t *reqlock = new_posix_lock (flock, transport, client_pid);

	int can_block = 0;

	switch (cmd) {
#if F_GETLK != F_GETLK64
	case F_GETLK64:
#endif

	case F_GETLK: {
		posix_lock_t *conf = pl_getlk (pl_inode, reqlock, GF_LOCK_POSIX);
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
		memcpy (&reqlock->user_flock, flock, sizeof (struct flock));

#if F_SETLK != F_SETLK64
	case F_SETLK64:
#endif
	case F_SETLK: {
		int ret = pl_setlk (pl_inode, reqlock, can_block, GF_LOCK_POSIX);

#ifdef _POSIX_LOCKS_DEBUG
		printf ("[SET] (ret=%d)", ret); print_lock (reqlock);
#endif

		pthread_mutex_unlock (&priv->mutex);

		if (ret == -1) {
			if (can_block)
				return 0;

			gf_log (this->name, GF_LOG_DEBUG, "returning EAGAIN");
			STACK_UNWIND (frame, ret, EAGAIN, flock);
			return 0;
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
	return 0;
}

/* TODO: this function just logs, no action required?? */
int32_t
pl_forget (xlator_t *this,
	   inode_t *inode)
{
	data_t *inode_data = dict_get (inode->ctx, this->name);
	if (inode_data) {
		pl_inode_t *pl_inode = (pl_inode_t *)data_to_bin (inode_data);
		if (pl_inode->rw_reqs) {
			gf_log (this->name, GF_LOG_CRITICAL,
				"Pending R/W requests found!");
		}
		if (pl_inode->fcntl_locks) {
			gf_log (this->name, GF_LOG_CRITICAL,
				"Active locks found!");
		}
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
		if (gf_string2boolean (mandatory->data, &priv->mandatory) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"'mandatory' takes only boolean options");
			return -1;
		}
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


int32_t
pl_inodelk (call_frame_t *frame, xlator_t *this, 
	    loc_t *loc, int32_t cmd, struct flock *flock);

int32_t
pl_finodelk (call_frame_t *frame, xlator_t *this, 
	     fd_t *fd, int32_t cmd, struct flock *flock);

int32_t
pl_entrylk (call_frame_t *frame, xlator_t *this, 
	    loc_t *loc, const char *basename, 
	    entrylk_cmd cmd, entrylk_type type);

int32_t
pl_fentrylk (call_frame_t *frame, xlator_t *this, 
	     fd_t *fd, const char *basename, 
	     entrylk_cmd cmd, entrylk_type type);

struct xlator_fops fops = {
	.create      = pl_create,
	.truncate    = pl_truncate,
	.ftruncate   = pl_ftruncate,
	.open        = pl_open,
	.readv       = pl_readv,
	.writev      = pl_writev,
	.lk          = pl_lk,
	.inodelk     = pl_inodelk,
	.finodelk    = pl_finodelk,
	.entrylk     = pl_entrylk,
	.fentrylk    = pl_fentrylk,
	.flush       = pl_flush,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
	.forget      = pl_forget,
};

struct xlator_options options[] = {
	{ "mandatory", GF_OPTION_TYPE_BOOL, 0, },
	{ NULL, 0, 0, 0, 0 },
};
