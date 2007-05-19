/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <asm/types.h>   /* for BITS_PER_LONG */

#include "glusterfs.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"
#include "posix-locks.h"

/* Forward declarations */

static posix_lock_t * delete_lock (pl_inode_t *, posix_lock_t *);
static pl_rw_req_t * delete_rw_req (pl_inode_t *, pl_rw_req_t *);
static void destroy_lock (posix_lock_t *);
static void do_blocked_rw (pl_inode_t *);
static int rw_allowable (pl_inode_t *, posix_lock_t *, rw_op_t);

/* cleanup inode */
void
pl_inode_release (pl_inode_t *inode)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    posix_lock_t *tmp = l;
    l = l->next;
    delete_lock (inode, tmp);
    destroy_lock (tmp);
  }

  pl_rw_req_t *rw = inode->rw_reqs;
  while (rw) {
    pl_rw_req_t *tmp = rw;
    rw = rw->next;
    delete_rw_req (inode, tmp);
    free (tmp);
  }
}

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

  lock->fl_start = flock->l_start;
  lock->fl_type = flock->l_type;

  if (flock->l_len == 0)
    lock->fl_end = ULONG_MAX;
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
  flock->l_len   = lock->fl_end == ULONG_MAX ? 0 : lock->fl_end - lock->fl_start + 1;
  flock->l_pid   = lock->client_pid;
}

/* Insert the lock into the inode's lock list */
static posix_lock_t *
insert_lock (pl_inode_t *inode, posix_lock_t *lock)
{
  lock->next = inode->locks;
  lock->prev = NULL;
  if (inode->locks)
    inode->locks->prev = lock;
  inode->locks = lock;
  return lock;
}

/* Delete a lock from the inode's lock list */
static posix_lock_t *
delete_lock (pl_inode_t *inode, posix_lock_t *lock)
{
  if (lock == inode->locks) {
    inode->locks = lock->next;
    if (inode->locks)
      inode->locks->prev = NULL;
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
delete_unlck_locks (pl_inode_t *inode)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    if (l->fl_type == F_UNLCK) {
      delete_lock (inode, l);
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
    memcpy (v.locks[0], big, sizeof (posix_lock_t));
    v.locks[0]->fl_type = small->fl_type;
  }
  else if ((small->fl_start > big->fl_start) &&
	   (small->fl_end   < big->fl_end)) {
    /* both edges lie inside big */
    v.locks[0] = calloc (1, sizeof (posix_lock_t));
    v.locks[1] = calloc (1, sizeof (posix_lock_t));
    v.locks[2] = calloc (1, sizeof (posix_lock_t));

    memcpy (v.locks[0], big, sizeof (posix_lock_t));
    v.locks[0]->fl_end = small->fl_start - 1;

    memcpy (v.locks[1], small, sizeof (posix_lock_t));
    memcpy (v.locks[2], big, sizeof (posix_lock_t));
    v.locks[2]->fl_start = small->fl_end;
  }
  /* one edge coincides with big */
  else if (small->fl_start == big->fl_start) {
    v.locks[0] = calloc (1, sizeof (posix_lock_t));
    v.locks[1] = calloc (1, sizeof (posix_lock_t));
    
    memcpy (v.locks[0], big, sizeof (posix_lock_t));
    v.locks[0]->fl_start   = small->fl_end;
    
    memcpy (v.locks[1], small, sizeof (posix_lock_t));
  }
  else if (small->fl_end   == big->fl_end) {
    v.locks[0] = calloc (1, sizeof (posix_lock_t));
    v.locks[1] = calloc (1, sizeof (posix_lock_t));

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
grant_blocked_locks (pl_inode_t *inode)
{
  posix_lock_t *l = inode->locks;

  while (l) {
    if (l->blocked) {
      posix_lock_t *conf = first_overlap (inode, l, inode->locks);
      if (conf == NULL) {
	l->blocked = 0;
	posix_lock_to_flock (l, l->user_flock);

	STACK_UNWIND (l->frame, 0, 0, l->user_flock);
      }
    }
    
    l = l->next;
  }
}

static posix_lock_t *
posix_getlk (pl_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *conf = first_overlap (inode, lock, inode->locks);
  if (conf == NULL) {
    lock->fl_type = F_UNLCK;
    return lock;
  }

  return conf;
}

#ifdef __POSIX_LOCKS_DEBUG
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
  /*  
      printf (" (%u, ", lock->fl_start);
      printf ("%u), ", lock->fl_end);
      printf ("pid = %u\n", lock->client_pid); */
  fflush (stdout);
}
#endif

/* Return true if lock is grantable */
static int
lock_grantable (pl_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *l = inode->locks;
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
insert_and_merge (pl_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *conf = first_overlap (inode, lock, inode->locks);
  while (conf) {
    if (same_owner (conf, lock)) {
      if (conf->fl_type == lock->fl_type) {
	posix_lock_t *sum = add_locks (lock, conf);
	sum->fl_type    = lock->fl_type;
	sum->transport  = lock->transport;
	sum->client_pid = lock->client_pid;

	delete_lock (inode, conf); destroy_lock (conf);
	destroy_lock (lock);
	insert_and_merge (inode, sum);
	return;
      }
      else {
	posix_lock_t *sum = add_locks (lock, conf);
	int i;

	sum->fl_type    = conf->fl_type;
	sum->transport  = conf->transport;
	sum->client_pid = conf->client_pid;

	struct _values v = subtract_locks (sum, lock);
	
	delete_lock (inode, conf);
	destroy_lock (conf);

	for (i = 0; i < 3; i++) {
	  if (v.locks[i]) {
	    insert_and_merge (inode, v.locks[i]);
	  }
	}

	delete_unlck_locks (inode);
	do_blocked_rw (inode);
	grant_blocked_locks (inode);
	return; 
      }
    }

    if (lock->fl_type == F_UNLCK) {
      conf = first_overlap (inode, lock, conf->next);
      continue;
    }

    if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
      insert_lock (inode, lock);
      return;
    }
  }

  /* no conflicts, so just insert */
  if (lock->fl_type != F_UNLCK) {
    insert_lock (inode, lock);
  }
}

static int
posix_setlk (pl_inode_t *inode, posix_lock_t *lock, int can_block)
{
  errno = 0;

  if (lock_grantable (inode, lock)) {
    insert_and_merge (inode, lock);
  }
  else if (can_block) {
    lock->blocked = 1;
    insert_lock (inode, lock);
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
  void *inode_or_fd;
  off_t offset;
  enum {TRUNCATE, FTRUNCATE} op;
};

static int32_t
pl_truncate_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

int 
truncate_allowed (pl_inode_t *inode, 
		  transport_t *transport, pid_t client_pid, 
		  off_t offset)
{
  posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
  region->fl_start = offset;
  region->fl_end   = ULONG_MAX;
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
truncate_getattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct stat *buf)
{
  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  struct _truncate_ops *local = (struct _truncate_ops *)frame->local;
  dict_t *inode_ctx;

  if (local->op == TRUNCATE)
    inode_ctx = ((inode_t *)local->inode_or_fd)->ctx;
  else
    inode_ctx = ((fd_t *)local->inode_or_fd)->inode->ctx;

  data_t *inode_data = dict_get (inode_ctx, this->name);
  if (inode_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, NULL);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

  if (inode && priv->mandatory && inode->mandatory &&
      !truncate_allowed (inode, frame->root->state,
			 frame->root->pid, local->offset)) {
    STACK_UNWIND (frame, -1, EAGAIN, buf);
    return 0;
  }

  switch (local->op) {
  case TRUNCATE:
    STACK_WIND (frame, pl_truncate_cbk,
		FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
		(inode_t *)local->inode_or_fd, local->offset);
    break;
  case FTRUNCATE:
    STACK_WIND (frame, pl_truncate_cbk,
		FIRST_CHILD (this), FIRST_CHILD (this)->fops->ftruncate,
		(fd_t *)local->inode_or_fd, local->offset);
    break;
  }

  return 0;
}

static int32_t 
pl_truncate (call_frame_t *frame, xlator_t *this,
	     inode_t *inode, off_t offset)
{
  GF_ERROR_IF_NULL (this);

  struct _truncate_ops *local = calloc (1, sizeof (struct _truncate_ops));
  local->inode_or_fd = inode;
  local->offset     = offset;
  local->op         = TRUNCATE;

  frame->local = local;

  STACK_WIND (frame, truncate_getattr_cbk, 
	      FIRST_CHILD (this), FIRST_CHILD (this)->fops->getattr,
	      inode);

  return 0;
}

static int32_t 
pl_ftruncate (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, off_t offset)
{
  struct _truncate_ops *local = calloc (1, sizeof (struct _truncate_ops));

  local->inode_or_fd = fd;
  local->offset      = offset;
  local->op          = FTRUNCATE;

  frame->local = local;

  STACK_WIND (frame, truncate_getattr_cbk, 
	      FIRST_CHILD(this), FIRST_CHILD(this)->fops->fgetattr, 
	      fd);
  return 0;
}

static int32_t 
pl_release_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret,	int32_t op_errno)
{
  GF_ERROR_IF_NULL (this);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
pl_release (call_frame_t *frame, xlator_t *this,
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
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

  data_t *inode_data = dict_get (fd->inode->ctx, this->name);
  if (inode_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

  dict_del (fd->ctx, this->name);
  dict_del (fd->inode->ctx, this->name);

  do_blocked_rw (inode);
  grant_blocked_locks (inode);

  pl_inode_release (inode);

  free (pfd);
  free (inode);

  pthread_mutex_unlock (&priv->mutex);

  STACK_WIND (frame, pl_release_cbk, 
	      FIRST_CHILD(this), FIRST_CHILD(this)->fops->release, 
	      fd);
  return 0;
}

static int32_t 
pl_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static void
delete_locks_of_pid (pl_inode_t *inode, pid_t pid)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    posix_lock_t *tmp = l;
    l = l->next;
    if (tmp->client_pid == pid) {
      delete_lock (inode, tmp);
      destroy_lock (tmp);
    }
  }
}

static int32_t 
pl_flush (call_frame_t *frame, xlator_t *this,
	  fd_t *fd)
{
  data_t *inode_data = dict_get (fd->inode->ctx, this->name);
  if (inode_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

  delete_locks_of_pid (inode, frame->root->pid);
  do_blocked_rw (inode);
  grant_blocked_locks (inode);

  STACK_WIND (frame, pl_flush_cbk, 
	      FIRST_CHILD(this), FIRST_CHILD(this)->fops->flush, 
	      fd);
  return 0;
}

struct _flags {
  int32_t flags;
};

static int32_t 
pl_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	     int32_t op_ret, int32_t op_errno, fd_t *fd)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fd);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  if (op_ret == 0) {
    pl_fd_t *pfd = calloc (1, sizeof (pl_fd_t));

    struct _flags *local = frame->local;
    if (frame->local)
      pfd->nonblocking = local->flags & O_NONBLOCK;

    pl_inode_t *inode = calloc (1, sizeof (pl_inode_t));

    struct stat buf = fd->inode->buf;
    if ((buf.st_mode & S_ISGID) && !(buf.st_mode & S_IXGRP))
      inode->mandatory = 1;

    if (!fd->inode) {
      gf_log (this->name, GF_LOG_ERROR, "fd->inode is NULL!");
      STACK_UNWIND (frame, -1, EBADFD, fd);
      return 0;
    }

    dict_set (fd->inode->ctx, this->name, bin_to_data (inode, sizeof (inode)));
    dict_set (fd->ctx, this->name, bin_to_data (pfd, sizeof (pfd)));
  }

  pthread_mutex_unlock (&priv->mutex);

  STACK_UNWIND (frame, op_ret, op_errno, fd);
  return 0;
}

static int32_t 
pl_open (call_frame_t *frame, xlator_t *this,
	 inode_t *inode, int32_t flags, mode_t mode)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (inode);

  struct _flags *f = calloc (1, sizeof (struct _flags));
  f->flags = flags;

  if (flags & O_RDONLY)
    f->flags &= ~O_TRUNC;

  frame->local = f;

  STACK_WIND (frame, pl_open_cbk, 
              FIRST_CHILD(this), FIRST_CHILD(this)->fops->open, 
              inode, flags & ~O_TRUNC, mode);

  return 0;
}

static int32_t 
pl_create (call_frame_t *frame, xlator_t *this,
	   inode_t *inode, const char *path, int32_t flags, mode_t mode)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  STACK_WIND (frame, pl_open_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create, 
	      inode, path, flags, mode);
  return 0;
}

static int32_t
pl_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno,
	      struct iovec *vector, int32_t count)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (vector);

  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
pl_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
	       int32_t op_ret, int32_t op_errno)
{
  GF_ERROR_IF_NULL (this);

  STACK_UNWIND (frame, op_ret, op_errno);
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
      case OP_WRITE: {
	dict_t *req_refs = rw->frame->root->req_refs;
	STACK_WIND (rw->frame, pl_writev_cbk,
		    FIRST_CHILD (rw->this), FIRST_CHILD (rw->this)->fops->writev,
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

static int32_t
pl_readv (call_frame_t *frame, xlator_t *this,
	  fd_t *fd, size_t size, off_t offset)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fd);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  data_t *fd_data = dict_get (fd->ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  pl_fd_t *pfd = (pl_fd_t *) data_to_bin (fd_data);

  data_t *inode_data = dict_get (fd->inode->ctx, this->name);
  if (inode_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

  if (priv->mandatory && inode->mandatory) {
    posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
    region->fl_start = offset;
    region->fl_end   = offset + size - 1;
    region->transport = frame->root->state;
    region->client_pid = frame->root->pid;
    
    if (!rw_allowable (inode, region, OP_READ)) {
      if (pfd->nonblocking) {
	pthread_mutex_unlock (&priv->mutex);
	STACK_UNWIND (frame, -1, EWOULDBLOCK);
	return -1;
      }

      pl_rw_req_t *rw = calloc (1, sizeof (pl_rw_req_t));
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

static int32_t 
pl_writev (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, struct iovec *vector, int32_t count, off_t offset)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fd);
  GF_ERROR_IF_NULL (vector);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  data_t *fd_data = dict_get (fd->ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

  data_t *inode_data = dict_get (fd->inode->ctx, this->name);
  if (inode_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);

  if (priv->mandatory && inode->mandatory) {
    int size = iovec_total_length (vector, count);

    posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
    region->fl_start = offset;
    region->fl_end   = offset + size - 1;
    region->transport = frame->root->state;
    region->client_pid = frame->root->pid;

    if (!rw_allowable (inode, region, OP_WRITE)) {
      if (pfd->nonblocking) {
	pthread_mutex_unlock (&priv->mutex);
	STACK_UNWIND (frame, -1, EWOULDBLOCK);
	return -1;
      }

      pl_rw_req_t *rw = calloc (1, sizeof (pl_rw_req_t));
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

static int32_t
pl_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd,
       struct flock *flock)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fd);
  GF_ERROR_IF_NULL (flock);

  transport_t *transport = frame->root->state;
  pid_t client_pid = frame->root->pid;
  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  struct flock nulllock = {0, };
  data_t *fd_data = dict_get (fd->ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  pl_fd_t *pfd = (pl_fd_t *)data_to_bin (fd_data);

  if (!pfd) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, nulllock);
    return -1;
  }

  data_t *inode_data = dict_get (fd->inode->ctx, this->name);
  if (inode_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  pl_inode_t *inode = (pl_inode_t *)data_to_bin (inode_data);
  
  posix_lock_t *reqlock = new_posix_lock (flock, transport, client_pid);

  int can_block = 0;

  switch (cmd) {
  case F_GETLK: {
    posix_lock_t *conf = posix_getlk (inode, reqlock);
    posix_lock_to_flock (conf, flock);
    pthread_mutex_unlock (&priv->mutex);
    destroy_lock (reqlock);
    STACK_UNWIND (frame, 0, 0, flock);
    return 0;
  }

  case F_SETLKW:
    can_block = 1;
    reqlock->frame = frame;
    reqlock->this  = this;
    reqlock->fd    = fd;
    reqlock->user_flock = calloc (1, sizeof (struct flock));
    memcpy (reqlock->user_flock, flock, sizeof (struct flock));
  case F_SETLK: {
    int ret = posix_setlk (inode, reqlock, can_block);
    pthread_mutex_unlock (&priv->mutex);

    if (can_block && (ret == -1)) {
      return -1;
    }

    if (ret == 0) {
      STACK_UNWIND (frame, ret, 0, flock);
      return 0;
    }
  }
  }

  pthread_mutex_unlock (&priv->mutex);
  STACK_UNWIND (frame, -1, -EAGAIN, flock);
  return -1;
}

int32_t
init (xlator_t *this)
{
  if (!this->children) {
    gf_log (this->name, GF_LOG_ERROR, "FATAL: posix-locks should have exactly one child");
    return -1;
  }

  if (this->children->next) {
    gf_log (this->name, GF_LOG_ERROR, "FATAL: posix-locks should have exactly one child");
    return -1;
  }

  posix_locks_private_t *priv = calloc (1, sizeof (posix_locks_private_t));
  pthread_mutex_init (&priv->mutex, NULL);

  data_t *mandatory = dict_get (this->options, "mandatory");
  if (mandatory) {
    priv->mandatory = 1;
  }

  this->private = priv;
  return 0;
}

int32_t
fini (xlator_t *this)
{
  return 0;
}

struct xlator_fops fops = {
  .create      = pl_create,
  .truncate    = pl_truncate,
  .ftruncate   = pl_ftruncate,
  .open        = pl_open,
  .readv       = pl_readv,
  .writev      = pl_writev,
  .release     = pl_release,
  .lk          = pl_lk,
  .flush       = pl_flush  
};

struct xlator_mops mops = {
};
