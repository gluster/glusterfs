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
#include "posix-locks.h"
#include "logging.h"

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

static posix_lock_t *
delete_lock (posix_inode_t *inode, posix_lock_t *lock);

/* Decrement the inode's ref count, free if necessary */
void
inode_dec_ref (posix_inode_t *inodes[], posix_inode_t *inode)
{
  inode->refcount--;
  if (inode->refcount == 0) {
    /* Release all locks, just to be sure */
    posix_lock_t *l = inode->locks;
    while (l) {
      posix_lock_t *tmp = l;
      l = l->next;
      delete_lock (inode, tmp);
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
static void
delete_unlck_locks (posix_inode_t *inode)
{
  posix_lock_t *l = inode->locks;
  while (l) {
    if (l->fl_type == F_UNLCK) {
      delete_lock (inode, l);
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
    v.locks[0]->fl_end = small->fl_start;

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
    v.locks[0]->fl_end = small->fl_start;
    
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
first_conflict (posix_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *l = inode->locks;

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
grant_blocked_locks (posix_inode_t *inode)
{
  posix_lock_t *l = inode->locks;

  while (l) {
    if (l->blocked) {
      posix_lock_t *conf = first_conflict (inode, l);
      if (conf == NULL) {
	l->blocked = 0;
	posix_lock_to_flock (l, l->user_flock);
	STACK_UNWIND (l->frame, 0, errno, l->user_flock);
      }
    }
    
    l = l->next;
  }
}

static posix_lock_t *
posix_getlk (posix_inode_t *inode, posix_lock_t *lock)
{
  posix_lock_t *conf = first_conflict (inode, lock);
  if (conf == NULL) {
    lock->fl_type = F_UNLCK;
    return lock;
  }

  return conf;
}

static int
posix_setlk (posix_inode_t *inode, posix_lock_t *lock, int can_block)
{
  posix_lock_t *conf = first_conflict (inode, lock);
  if (conf) {
    if (same_owner (conf, lock)) {
      if (conf->fl_type == lock->fl_type) {
	posix_lock_t *sum = add_locks (lock, conf);
	sum->fl_type    = lock->fl_type;
	sum->pfd        = lock->pfd;
	sum->transport  = lock->transport;
	sum->client_pid = lock->client_pid;

	delete_lock (inode, conf); free (conf);
	delete_lock (inode, lock); free (lock);
	insert_lock (inode, sum);
	return 0;
      }
      else {
	posix_lock_t *sum = add_locks (lock, conf);
	int i;

	sum->fl_type    = conf->fl_type;
	sum->pfd        = conf->pfd;
	sum->transport  = conf->transport;
	sum->client_pid = conf->client_pid;

	struct _values v = subtract_locks (sum, lock);
	
	delete_lock (inode, conf);
	free (conf);

	for (i = 0; i < 3; i++) {
	  if (v.locks[i])
	    insert_lock (inode, v.locks[i]);
	}

	delete_unlck_locks (inode);
	grant_blocked_locks (inode);
	return 0;
      }
    }

    if ((conf->fl_type == F_WRLCK) || 
	(lock->fl_type == F_WRLCK)) {
      
      if (can_block) {
	lock->blocked = 1;
	insert_lock (inode, lock);
	return 0;
      }

      return -1;
    }
  }

  /* no conflicts, so just insert */
  insert_lock (inode, lock);
  return 0;
}

/* fops */
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

  if (op_ret == 0) {
    ino_t ino = buf->st_ino;
    posix_fd_t *pfd = calloc (1, sizeof (posix_fd_t));
    posix_inode_t *inode = lookup_inode (priv->inodes, ino);

    if (inode == NULL) {
      inode = create_inode (priv->inodes, ino);
    }
    
    pfd->inode = inode;
    inode_inc_ref (inode);
    dict_set (ctx, this->name, int_to_data ((uint64_t )pfd));
  }

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

  STACK_WIND (frame, posix_locks_open_cbk, 
              FIRST_CHILD(this), 
              FIRST_CHILD(this)->fops->open, 
              path, flags, mode);
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

  struct flock nulllock = {0, };
  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF, &nulllock);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *)data_to_int (fd_data);

  /* We don't need to release all the locks associated with this fd becase
     FUSE will send appropriate F_UNLCK requests for that */
  dict_del (ctx, this->name);
  inode_dec_ref (((posix_locks_private_t *)this->private)->inodes, 
		 pfd->inode);
  free (pfd);
  
  STACK_WIND (frame, 
	      posix_locks_release_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->release, 
	      ctx);
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

  struct flock nulllock = {0, };
  data_t *fd_data = dict_get (ctx, this->name);
  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, -EBADF, &nulllock);
    return 0;
  }
  posix_fd_t *pfd = (posix_fd_t *)data_to_int (fd_data);

  if (!pfd) {
    STACK_UNWIND (frame, -1, -EBADF, nulllock);
    return -1;
  }

  posix_lock_t *reqlock = new_posix_lock (flock, transport, client_pid);

  int can_block = 0;

  switch (cmd) {
  case F_GETLK: {
    posix_lock_t *conf = posix_getlk (pfd->inode, reqlock);
    posix_lock_to_flock (conf, flock);
    STACK_UNWIND (frame, 0, 0, flock);
    return 0;
  }

  case F_SETLKW:
    can_block = 1;
    reqlock->frame = frame;
    reqlock->user_flock = flock;
  case F_SETLK: {
    int ret = posix_setlk (pfd->inode, reqlock, can_block);
    if (!can_block)
      STACK_UNWIND (frame, ret, errno, flock);
    return 0;
  }
  }

  STACK_UNWIND (frame, -1, errno, flock);
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
  pthread_mutex_init (&priv->locks_mutex, NULL);

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
  .open        = posix_locks_open,
  .create      = posix_locks_create,
  //  .readv       = posix_locks_readv,
  //  .writev      = posix_locks_writev,
  .release     = posix_locks_release,
  .lk          = posix_locks_lk
};

struct xlator_mops mops = {
};
