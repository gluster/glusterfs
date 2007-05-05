/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>

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
#include "logging.h"
#include "common-utils.h"
#include "posix-locks.h"

/* 
   "Multiplicative" hashing. GOLDEN_RATIO_PRIME value stolen from
   linux source.  See include/linux/hash.h in the source tree.
*/

#if BITS_PER_LONG == 32
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#elif BITS_PER_LONG == 64
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
#else
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#endif

static uint32_t integer_hash (uint32_t key, int size)
{
  uint32_t hash = key;
  hash *= GOLDEN_RATIO_PRIME;
  return hash % size;
}

/* Lookup an inode */
static posix_inode_t *
lookup_inode (posix_inode_t *inodes[], ino_t ino)
{
  uint32_t hash = integer_hash (ino, HASH_TABLE_SIZE);
  posix_inode_t *inode = inodes[hash];
  while (inode) {
    if (inode->ino == ino)
      return inode;
    inode = inode->hash_next;
  }

  return NULL;
}

/* Insert a new inode into the table */
static posix_inode_t *
create_inode (posix_inode_t *inodes[], ino_t ino)
{
  posix_inode_t *inode = calloc (1, sizeof (posix_inode_t));
  inode->ino = ino;
  uint32_t hash = integer_hash (ino, HASH_TABLE_SIZE);

  inode->hash_next = inodes[hash];
  inodes[hash] = inode;

  return inode;
}

/* Increment the inode's ref count */
void 
inode_inc_ref (posix_inode_t *inode)
{
  inode->refcount++;
}

static posix_lock_t * delete_lock ();
static posix_rw_req_t * delete_rw_req ();

/* Decrement the inode's ref count, free if necessary */
void
inode_dec_ref (posix_inode_t *inodes[], posix_inode_t *inode)
{
  inode->refcount--;
  if (inode->refcount <= 0) {
    /* Release all locks */
    posix_lock_t *l = inode->locks;
    while (l) {
      posix_lock_t *tmp = l;
      l = l->next;
      delete_lock (inode, tmp);
      free (tmp);
    }

    posix_rw_req_t *rw = inode->rw_reqs;
    while (rw) {
      //      rw = rw->next;
      delete_rw_req (inode, rw);
      free (rw);
    }

    int32_t hash = integer_hash (inode->ino, HASH_TABLE_SIZE);
    posix_inode_t *prev = inodes[hash];
    posix_inode_t *i    = inodes[hash];

    if (inode == inodes[hash]) {
      inodes[hash] = inode->hash_next;
      free (inode);
      return;
    }

    while (i) {
      if (i->ino == inode->ino) {
	prev->hash_next = i->hash_next;
	free (inode);
	return;
      }

      prev = i;
      i = i->hash_next;
    }
  }
}

/* Insert an rw request into the inode's rw list */
static posix_rw_req_t *
insert_rw_req (posix_inode_t *inode, posix_rw_req_t *rw)
{
  rw->next = inode->rw_reqs;
  rw->prev = NULL;
  if (inode->rw_reqs)
    inode->rw_reqs->prev = rw;
  inode->rw_reqs = rw;
  return rw;
}

/* Delete an rw request from the inode's rw list */
static posix_rw_req_t *
delete_rw_req (posix_inode_t *inode, posix_rw_req_t *rw)
{
  if (rw == inode->rw_reqs) {
    inode->rw_reqs = rw->next;
    if (inode->rw_reqs)
      inode->rw_reqs->prev = NULL;
  }
  else {
    posix_rw_req_t *prev = rw->prev;
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
insert_lock (posix_inode_t *inode, posix_lock_t *lock)
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
delete_lock (posix_inode_t *inode, posix_lock_t *lock)
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
static void print_lock ();
static void
delete_unlck_locks (posix_inode_t *inode)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    if (l->fl_type == F_UNLCK) {
      delete_lock (inode, l);
      printf ("  Deleting: "); print_lock (l);
      free (l);
    }

    l = l->next;
  }
}

/* Add two locks */

static posix_lock_t *
add_locks (posix_lock_t *l1, posix_lock_t *l2)
{
  off_t min (off_t x, off_t y)
  {
    return x < y ? x : y;
  }

  off_t max (off_t x, off_t y)
  {
    return x > y ? x : y;
  }

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
first_overlap (posix_inode_t *inode, posix_lock_t *lock,
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

static void print_lock ();

static void
grant_blocked_locks (posix_inode_t *inode)
{
  posix_lock_t *l = inode->locks;

  while (l) {
    if (l->blocked) {
      posix_lock_t *conf = first_overlap (inode, l, inode->locks);
      if (conf == NULL) {
	printf ("Granting blocked lock: "); print_lock (l);
	l->blocked = 0;
	posix_lock_to_flock (l, l->user_flock);

	STACK_UNWIND (l->frame, 0, 0, l->user_flock);
      }
    }
    
    l = l->next;
  }
}

static posix_lock_t *
posix_getlk (posix_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *conf = first_overlap (inode, lock, inode->locks);
  if (conf == NULL) {
    lock->fl_type = F_UNLCK;
    return lock;
  }

  return conf;
}

static void
do_blocked_rw (posix_inode_t *inode);

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

/* Return true if lock is grantable */
static int
lock_grantable (posix_inode_t *inode, posix_lock_t *lock)
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
insert_and_merge (posix_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *conf = first_overlap (inode, lock, inode->locks);
  while (conf) {
    printf ("  Conflict with: "); print_lock (conf);
    if (same_owner (conf, lock)) {
      if (conf->fl_type == lock->fl_type) {
	posix_lock_t *sum = add_locks (lock, conf);
	sum->fl_type    = lock->fl_type;
	sum->transport  = lock->transport;
	sum->client_pid = lock->client_pid;

	printf ("  Deleting: "); print_lock (conf);
	delete_lock (inode, conf); free (conf);
	free (lock);
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
	free (conf);

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
  }

  /* no conflicts, so just insert */
  if (lock->fl_type != F_UNLCK) {
    printf ("   Inserting: "); print_lock (lock);
    insert_lock (inode, lock);
  }
}

static int
posix_setlk (posix_inode_t *inode, posix_lock_t *lock, int can_block)
{
  errno = 0;
  printf ("Trying to set: ");
  print_lock (lock);

  if (lock_grantable (inode, lock)) {
    printf ("  Is grantable: "); print_lock (lock);
    insert_and_merge (inode, lock);
  }
  else if (can_block) {
    lock->blocked = 1;
    printf ("  Blocking: "); print_lock (lock);
    insert_lock (inode, lock);
    return -1;
  }
  else {
    printf ("  Not grantable: "); print_lock (lock);
    errno = EAGAIN;
    return -1;
  }

  return 0;
}

/* fops */

struct _truncate_ops {
  const char *path;
  off_t offset;
};

static int32_t
posix_locks_truncate_cbk (call_frame_t *frame, call_frame_t *prev_frame,
			  xlator_t *this, int32_t op_ret, int32_t op_errno,
			  struct stat *buf)
{
  struct _truncate_ops *local = (struct _truncate_ops *)frame->local;
  free ((char *) local->path);
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
truncate_getattr_cbk (call_frame_t *frame, call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  ino_t ino = buf->st_ino;
  posix_inode_t *inode = lookup_inode (priv->inodes, ino);
  struct _truncate_ops *local = (struct _truncate_ops *)frame->local;

  if (inode && priv->mandatory && inode->mandatory &&
      inode->locks) {
    free ((char *)local->path);
    STACK_UNWIND (frame, -1, EAGAIN, buf);
    return 0;
  }


  STACK_WIND (frame, posix_locks_truncate_cbk,
	      FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
	      local->path, local->offset);

  return 0;
}

static int32_t 
posix_locks_truncate (call_frame_t *frame,
		      xlator_t *this,
		      const char *path,
		      off_t offset)
{
  /* check pid */
  GF_ERROR_IF_NULL (this);

  struct _truncate_ops *local = calloc (1, sizeof (struct _truncate_ops));
  local->path = strdup (path);
  local->offset = offset;

  frame->local = local;

  STACK_WIND (frame, truncate_getattr_cbk, 
	      FIRST_CHILD (this), FIRST_CHILD (this)->fops->getattr,
	      path);

  return 0;
}

static int32_t 
posix_locks_release_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  GF_ERROR_IF_NULL (this);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
posix_locks_release (call_frame_t *frame,
		     xlator_t *this,
		     dict_t *ctx)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  struct flock nulllock = {0, };
  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *)data_to_bin (fd_data);

  /* We don't need to release all the locks associated with this fd becase
     FUSE will send appropriate F_UNLCK requests for that */

  dict_del (ctx, this->name);
  inode_dec_ref (((posix_locks_private_t *)this->private)->inodes, 
		 pfd->inode);
  free (pfd);

  pthread_mutex_unlock (&priv->mutex);

  STACK_WIND (frame, 
	      posix_locks_release_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->release, 
	      ctx);
  return 0;
}

static int32_t 
posix_locks_flush_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static void
delete_locks_of_pid (posix_inode_t *inode, pid_t pid)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    posix_lock_t *tmp = l;
    l = l->next;
    if (l->client_pid == pid) {
      delete_lock (inode, tmp);
      free (tmp);
    }
  }
}

static int32_t 
posix_locks_flush (call_frame_t *frame,
		   xlator_t *this,
		   dict_t *ctx)
{
  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *) data_to_bin (fd_data);
  posix_inode_t *inode = pfd->inode;

  delete_locks_of_pid (inode, frame->root->pid);

  STACK_WIND (frame, 
	      posix_locks_flush_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->flush, 
	      ctx);
  return 0;
}

struct _flags {
  int32_t flags;
};

static int32_t 
posix_locks_open_cbk (call_frame_t *frame, call_frame_t *prev_frame,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      dict_t *ctx,
                      struct stat *buf)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (prev_frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (buf);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  if (op_ret == 0) {
    ino_t ino = buf->st_ino;
    posix_fd_t *pfd = calloc (1, sizeof (posix_fd_t));

    struct _flags *local = frame->local;
    if (frame->local)
      pfd->nonblocking = local->flags & O_NONBLOCK;

    posix_inode_t *inode = lookup_inode (priv->inodes, ino);

    if (inode == NULL) {
      inode = create_inode (priv->inodes, ino);
    }

    if ((buf->st_mode & S_ISGID) && !(buf->st_mode & S_IXGRP))
      inode->mandatory = 1;

    pfd->inode = inode;
    inode_inc_ref (inode);
    dict_set (ctx, this->name, bin_to_data (pfd, sizeof (pfd)));
  }

  pthread_mutex_unlock (&priv->mutex);

  STACK_UNWIND (frame, op_ret, op_errno, ctx, buf);
  return 0;
}

static int32_t 
posix_locks_open (call_frame_t *frame,
                  xlator_t *this,
                  const char *path,
                  int32_t flags,
                  mode_t mode)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  struct _flags *f = calloc (1, sizeof (struct _flags));
  f->flags = flags;

  if (flags & O_RDONLY)
    f->flags &= ~O_TRUNC;

  frame->local = f;

  STACK_WIND (frame, posix_locks_open_cbk, 
              FIRST_CHILD(this), 
              FIRST_CHILD(this)->fops->open, 
              path, flags & ~O_TRUNC, mode);

  return 0;
}

static int32_t 
posix_locks_create (call_frame_t *frame,
		    xlator_t *this,
		    const char *path,
		    mode_t mode)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  STACK_WIND (frame, posix_locks_open_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
              path, mode);
  return 0;
}

static int32_t
posix_locks_readv_cbk (call_frame_t *frame, 
		       call_frame_t *prev_frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct iovec *vector,
		       int32_t count)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (vector);

  // XXX: ?? dict_ref (frame->root->req_refs); 
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
posix_locks_writev_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  GF_ERROR_IF_NULL (this);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static void
do_blocked_rw (posix_inode_t *inode)
{
  posix_rw_req_t *rw = inode->rw_reqs;

  while (rw) {
    posix_lock_t *conf = first_overlap (inode, rw->region, inode->locks);
    if (conf == NULL) {
      switch (rw->op) {
      case OP_READ:
	STACK_WIND (rw->frame, posix_locks_readv_cbk,
		    FIRST_CHILD (rw->this), FIRST_CHILD (rw->this)->fops->readv,
		    rw->ctx, rw->size, rw->region->fl_start);
	break;
      case OP_WRITE: {
	dict_t *req_refs = rw->frame->root->req_refs;
	STACK_WIND (rw->frame, posix_locks_writev_cbk,
		    FIRST_CHILD (rw->this), FIRST_CHILD (rw->this)->fops->writev,
		    rw->ctx, rw->vector, rw->size, rw->region->fl_start);
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

static int32_t
posix_locks_readv (call_frame_t *frame,
		   xlator_t *this,
		   dict_t *fdctx,
		   size_t size,
		   off_t offset)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  data_t *fd_data = dict_get (fdctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *) data_to_bin (fd_data);
  posix_inode_t *inode = pfd->inode;

  if (priv->mandatory && inode->mandatory) {
    posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
    region->fl_start = offset;
    region->fl_end   = offset + size - 1;

    posix_lock_t *conf = first_overlap (inode, region, inode->locks);
    if (conf && (conf->fl_type == F_WRLCK)) {
      if (pfd->nonblocking) {
	pthread_mutex_unlock (&priv->mutex);
	STACK_UNWIND (frame, -1, EWOULDBLOCK);
	return -1;
      }

      posix_rw_req_t *rw = calloc (1, sizeof (posix_rw_req_t));
      rw->frame  = frame;
      rw->this   = this;
      rw->ctx    = fdctx;
      rw->op     = OP_READ;
      rw->size   = size;
      rw->region = region;

      insert_rw_req (inode, rw);
      pthread_mutex_unlock (&priv->mutex);
      return 0;
    }
  }


  pthread_mutex_unlock (&priv->mutex);

  STACK_WIND (frame, posix_locks_readv_cbk, 
	      FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
	      fdctx, size, offset);
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
posix_locks_writev (call_frame_t *frame,
		    xlator_t *this,
		    dict_t *ctx,
		    struct iovec *vector,
		    int32_t count,
		    off_t offset)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);
  GF_ERROR_IF_NULL (vector);

  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *)data_to_bin (fd_data);
  posix_inode_t *inode = pfd->inode;

  if (priv->mandatory && inode->mandatory) {
    int size = iovec_total_length (vector, count);

    posix_lock_t *region = calloc (1, sizeof (posix_lock_t));
    region->fl_start = offset;
    region->fl_end   = offset + size - 1;

    posix_lock_t *conf = first_overlap (inode, region, inode->locks);
    if (conf) {
      if (pfd->nonblocking) {
	pthread_mutex_unlock (&priv->mutex);
	STACK_UNWIND (frame, -1, EWOULDBLOCK);
	return -1;
      }

      posix_rw_req_t *rw = calloc (1, sizeof (posix_rw_req_t));
      dict_ref (frame->root->req_refs);
      rw->frame  = frame;
      rw->this   = this;
      rw->ctx    = ctx;
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

  STACK_WIND (frame, posix_locks_writev_cbk,
	      FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev, 
	      ctx, vector, count, offset);
  return 0;
}

static int32_t
posix_locks_lk (call_frame_t *frame,
                xlator_t *this,
                dict_t *ctx, 
                int32_t cmd,
                struct flock *flock)
{
  GF_ERROR_IF_NULL (frame);
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);
  GF_ERROR_IF_NULL (flock);

  transport_t *transport = frame->root->state;
  pid_t client_pid = frame->root->pid;
  posix_locks_private_t *priv = (posix_locks_private_t *)this->private;
  pthread_mutex_lock (&priv->mutex);

  struct flock nulllock = {0, };
  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *)data_to_bin (fd_data);

  if (!pfd) {
    pthread_mutex_unlock (&priv->mutex);
    STACK_UNWIND (frame, -1, EBADF, nulllock);
    return -1;
  }

  posix_lock_t *reqlock = new_posix_lock (flock, transport, client_pid);

  int can_block = 0;

  switch (cmd) {
  case F_GETLK: {
    posix_lock_t *conf = posix_getlk (pfd->inode, reqlock);
    posix_lock_to_flock (conf, flock);
    pthread_mutex_unlock (&priv->mutex);
    free (reqlock);
    STACK_UNWIND (frame, 0, 0, flock);
    return 0;
  }

  case F_SETLKW:
    can_block = 1;
    reqlock->frame = frame;
    reqlock->this  = this;
    reqlock->ctx   = ctx;
    reqlock->user_flock = flock;
  case F_SETLK: {
    int ret = posix_setlk (pfd->inode, reqlock, can_block);
    pthread_mutex_unlock (&priv->mutex);

    if (can_block && (ret == -1)) {
      //free (reqlock);
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
    gf_log ("posix-locks", GF_LOG_ERROR, "FATAL: posix-locks should have exactly one child");
    return -1;
  }

  if (this->children->next) {
    gf_log ("posix-locks", GF_LOG_ERROR, "FATAL: posix-locks should have exactly one child");
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
  .create      = posix_locks_create,
  .truncate    = posix_locks_truncate,
  //XXX: .ftruncate = posix_locks_ftruncate,
  .open        = posix_locks_open,
  .readv       = posix_locks_readv,
  .writev      = posix_locks_writev,
  .release     = posix_locks_release,
  .lk          = posix_locks_lk,
  .flush       = posix_locks_flush  
};

struct xlator_mops mops = {
};
